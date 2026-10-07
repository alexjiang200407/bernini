#include "pipeline/PipelineLayout_vulkan.h"
#include "resource/BindlessTable_vulkan.h"
#include "shadercache/ShaderCache_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <algorithm>
#include <array>
#include <bgpu/ProgramCache.h>
#include <bgpu/SlangErrorChecker.h>
#include <bgpu/reflection/ReflectedLayout.h>
#include <bgpu/reflection/SlangReflection.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/uniforms/UniformLayoutEntry.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bgpu::pipeline_util
{
	namespace
	{
		[[nodiscard]] std::vector<IShader*>
		OrderedShaders(const std::initializer_list<IShader*> shaders)
		{
			auto ordered = std::vector<IShader*>();
			for (IShader* shader : shaders)
			{
				if (shader != nullptr)
					ordered.push_back(shader);
			}
			return ordered;
		}

		// Links the composition and takes SPIR-V and reflection out of the one linked program, so the
		// code and the layout always agree. The slow path: GetSlangModule() compiles the source here.
		[[nodiscard]] CachedProgram
		CompileWithSlang(const std::vector<IShader*>& shaders)
		{
			core::ensure(!shaders.empty(), "A pipeline needs at least one shader to compile");

			auto                                           errChecker = SlangErrorChecker();
			slang::ISession*                               session    = nullptr;
			std::vector<slang::IComponentType*>            components;
			std::unordered_set<slang::IModule*>            uniqueModules;
			std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints;

			for (IShader* shader : shaders)
			{
				slang::IModule* module = shader->GetSlangModule();
				core::ensure(module != nullptr, "Shader module cannot be null");

				// Read off the module, so reaching this function is what creates a session on this
				// thread and a cache hit never does.
				session = module->getSession();

				if (uniqueModules.insert(module).second)
					components.emplace_back(module);

				Slang::ComPtr<slang::IEntryPoint> entryPoint;
				module->findEntryPointByName(
					shader->GetDesc().entryPointName.c_str(),
					entryPoint.writeRef());
				core::ensure(entryPoint != nullptr, "Failed to find entry point in module");

				components.emplace_back(entryPoint.get());
				entryPoints.emplace_back(std::move(entryPoint));
			}

			Slang::ComPtr<slang::IComponentType> program;
			session->createCompositeComponentType(
				components.data(),
				static_cast<SlangInt>(components.size()),
				program.writeRef(),
				errChecker.WriteDiagnosticBlob()) >>
				errChecker;
			core::ensure(program != nullptr, "Failed to compose shader modules");

			Slang::ComPtr<slang::IComponentType> linked;
			program->link(linked.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

			slang::ProgramLayout* layout = linked->getLayout();

			auto result = CachedProgram();

			auto entryPointIndexByName = std::unordered_map<std::string_view, SlangInt>();
			for (SlangUInt i = 0; i < layout->getEntryPointCount(); ++i)
			{
				entryPointIndexByName.emplace(
					layout->getEntryPointByIndex(i)->getName(),
					static_cast<SlangInt>(i));
			}

			for (IShader* shader : shaders)
			{
				const std::string& entryName = shader->GetDesc().entryPointName;

				const auto found = entryPointIndexByName.find(entryName);
				core::ensure(
					found != entryPointIndexByName.end(),
					"Entry point missing from linked program");

				Slang::ComPtr<slang::IBlob> code;
				linked->getEntryPointCode(
					found->second,
					0,
					code.writeRef(),
					errChecker.WriteDiagnosticBlob()) >>
					errChecker;
				core::ensure(code != nullptr, "Failed to generate entry point SPIR-V");

				const auto* bytes = static_cast<const std::byte*>(code->getBufferPointer());
				result.entryPointSpirv.push_back(
					{
						.entryPoint = entryName,
						.spirv      = std::vector<std::byte>(bytes, bytes + code->getBufferSize()),
					});
			}

			// On SPIR-V a constant buffer reflects as a descriptor-table slot, as every other
			// resource does, so its type names it where its category cannot.
			for (uint32_t i = 0; i < layout->getParameterCount(); ++i)
			{
				slang::VariableLayoutReflection* param      = layout->getParameterByIndex(i);
				slang::TypeLayoutReflection*     typeLayout = param->getTypeLayout();
				if (typeLayout->getKind() != slang::TypeReflection::Kind::ConstantBuffer)
					continue;

				slang::TypeLayoutReflection* elementLayout = typeLayout->getElementTypeLayout();

				auto cbuffer           = CachedCbuffer();
				cbuffer.name           = param->getName();
				cbuffer.size           = static_cast<uint32_t>(elementLayout->getSize());
				cbuffer.rootParamIndex = static_cast<uint32_t>(result.cbuffers.size());
				cbuffer.binding        = static_cast<uint32_t>(param->getBindingIndex());
				cbuffer.set            = static_cast<uint32_t>(param->getBindingSpace());
				cbuffer.layout         = ReflectLayoutFromSlang(elementLayout);
				result.cbuffers.push_back(std::move(cbuffer));
			}

			return result;
		}

		[[nodiscard]] PipelineLayout
		BuildLayoutFromProgram(
			const VkDevice               device,
			const std::vector<IShader*>& shaders,
			const CachedProgram&         program,
			const VkShaderStageFlags     stages)
		{
			auto result   = PipelineLayout();
			result.device = device;

			auto bindings = std::vector<VkDescriptorSetLayoutBinding>();
			for (const CachedCbuffer& cbuffer : program.cbuffers)
			{
				core::ensure(
					cbuffer.rootParamIndex == bindings.size(),
					"Cached constant buffer order is inconsistent");
				core::ensure(
					cbuffer.set == 0,
					"Constant buffer '{}' is in set {}: set 0 holds them, and set {} the bindless "
					"table",
					cbuffer.name,
					cbuffer.set,
					BindlessTable::c_Set);

				auto binding            = VkDescriptorSetLayoutBinding();
				binding.binding         = cbuffer.binding;
				binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				binding.descriptorCount = 1;
				binding.stageFlags      = stages;
				bindings.push_back(binding);
				result.cbufferBindings.push_back(cbuffer.binding);

				auto entry           = UniformLayoutEntry();
				entry.size           = cbuffer.size;
				entry.layout         = std::make_shared<const ReflectedLayout>(cbuffer.layout);
				entry.rootParamIndex = cbuffer.rootParamIndex;
				result.uniformLayoutEntries[cbuffer.name] = entry;
			}

			auto constantsInfo         = VkDescriptorSetLayoutCreateInfo();
			constantsInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			constantsInfo.bindingCount = static_cast<uint32_t>(bindings.size());
			constantsInfo.pBindings    = bindings.data();
			EnsureVk(
				vkCreateDescriptorSetLayout(
					device,
					&constantsInfo,
					nullptr,
					&result.constantsLayout),
				"vkCreateDescriptorSetLayout");

			result.bindlessLayout = BindlessTable::CreateSetLayout(device);

			static_assert(
				BindlessTable::c_Set == 1,
				"The constants' set and the table's are 0 and 1");
			const auto setLayouts =
				std::to_array({ result.constantsLayout, result.bindlessLayout });

			auto layoutInfo           = VkPipelineLayoutCreateInfo();
			layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
			layoutInfo.pSetLayouts    = setLayouts.data();
			EnsureVk(
				vkCreatePipelineLayout(device, &layoutInfo, nullptr, &result.layout),
				"vkCreatePipelineLayout");

			for (IShader* shader : shaders)
			{
				const std::string& entryName = shader->GetDesc().entryPointName;

				const auto found =
					std::ranges::find_if(program.entryPointSpirv, [&](const EntryPointSpirv& e) {
						return e.entryPoint == entryName;
					});
				core::ensure(
					found != program.entryPointSpirv.end(),
					"Cached program is missing SPIR-V for a shader");

				result.entryPointCode[entryName] = found->spirv;
			}

			return result;
		}
	}

	PipelineLayout::~PipelineLayout() noexcept { Destroy(); }

	PipelineLayout::PipelineLayout(PipelineLayout&& other) noexcept :
		device(std::exchange(other.device, VK_NULL_HANDLE)),
		constantsLayout(std::exchange(other.constantsLayout, VK_NULL_HANDLE)),
		bindlessLayout(std::exchange(other.bindlessLayout, VK_NULL_HANDLE)),
		layout(std::exchange(other.layout, VK_NULL_HANDLE)),
		uniformLayoutEntries(std::move(other.uniformLayoutEntries)),
		cbufferBindings(std::move(other.cbufferBindings)),
		entryPointCode(std::move(other.entryPointCode))
	{}

	PipelineLayout&
	PipelineLayout::operator=(PipelineLayout&& other) noexcept
	{
		if (this != &other)
		{
			Destroy();
			device               = std::exchange(other.device, VK_NULL_HANDLE);
			constantsLayout      = std::exchange(other.constantsLayout, VK_NULL_HANDLE);
			bindlessLayout       = std::exchange(other.bindlessLayout, VK_NULL_HANDLE);
			layout               = std::exchange(other.layout, VK_NULL_HANDLE);
			uniformLayoutEntries = std::move(other.uniformLayoutEntries);
			cbufferBindings      = std::move(other.cbufferBindings);
			entryPointCode       = std::move(other.entryPointCode);
		}
		return *this;
	}

	void
	PipelineLayout::Destroy() noexcept
	{
		if (device == VK_NULL_HANDLE)
			return;
		vkDestroyPipelineLayout(device, layout, nullptr);
		vkDestroyDescriptorSetLayout(device, bindlessLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, constantsLayout, nullptr);
		device = VK_NULL_HANDLE;
	}

	PipelineLayout
	BuildPipelineLayout(
		const VkDevice                        device,
		const ShaderCache*                    cache,
		const std::initializer_list<IShader*> shaders,
		const VkShaderStageFlags              stages)
	{
		core::ensure(device != VK_NULL_HANDLE, "A pipeline layout needs a device");

		const std::vector<IShader*> ordered = OrderedShaders(shaders);

		auto     program     = CachedProgram();
		bool     haveProgram = false;
		uint64_t key         = 0;

		if (cache != nullptr)
		{
			auto moduleEntries = std::vector<ProgramEntryPoint>();
			moduleEntries.reserve(ordered.size());
			for (IShader* shader : ordered)
			{
				moduleEntries.push_back(
					{
						.moduleName = shader->GetDesc().slangModuleName,
						.entryPoint = shader->GetDesc().entryPointName,
					});
			}

			key         = cache->ComputeKey(std::move(moduleEntries));
			haveProgram = cache->TryLoad(key, program);
		}

		if (!haveProgram)
		{
			program = CompileWithSlang(ordered);
			if (cache != nullptr)
				cache->Store(key, program);
		}

		return BuildLayoutFromProgram(device, ordered, program, stages);
	}
}
