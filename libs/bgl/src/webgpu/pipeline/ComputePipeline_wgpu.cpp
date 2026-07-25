#include "pipeline/ComputePipeline_wgpu.h"

#include "resource/Shader.h"
#include "slang/SlangErrorChecker.h"

#include <bgl/IGraphics.h>

namespace bgl
{
	namespace
	{
		struct BindGroupSlot
		{
			uint32_t group;
			uint32_t binding;
			bool     readWrite;
		};

		struct ReflectedProgram
		{
			std::string           wgsl;
			slang::ProgramLayout* layout = nullptr;
		};

		// Links the shader's entry point, keeping the program layout alive on `owner` so the layout
		// pointer stays valid while reflection reads it.
		ReflectedProgram
		LinkProgram(
			slang::ISession*                      session,
			IShader*                              shader,
			Slang::ComPtr<slang::IComponentType>& owner)
		{
			SlangErrorChecker errChecker;

			slang::IModule* module = shader->GetSlangModule();
			gassert(module != nullptr, "ComputePipeline: null shader module");

			const std::string& entryName = shader->GetDesc().entryPointName.empty() ?
			                                   "main" :
			                                   shader->GetDesc().entryPointName;

			auto entryPoint = Slang::ComPtr<slang::IEntryPoint>();
			module->findEntryPointByName(entryName.c_str(), entryPoint.writeRef());
			if (entryPoint == nullptr)
				throw GraphicsError("wgsl: compute entry point not found: " + entryName);

			slang::IComponentType* components[] = { module, entryPoint.get() };

			auto program = Slang::ComPtr<slang::IComponentType>();
			session->createCompositeComponentType(
				components,
				std::size(components),
				program.writeRef(),
				errChecker.WriteDiagnosticBlob()) >>
				errChecker;

			program->link(owner.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

			auto code = Slang::ComPtr<slang::IBlob>();
			owner->getEntryPointCode(0, 0, code.writeRef(), errChecker.WriteDiagnosticBlob()) >>
				errChecker;

			return { std::string(
						 static_cast<const char*>(code->getBufferPointer()),
						 code->getBufferSize()),
				     owner->getLayout() };
		}

		bool
		IsReadWrite(slang::TypeReflection* type) noexcept
		{
			return type->getResourceAccess() == SLANG_RESOURCE_ACCESS_READ_WRITE;
		}

		// Walks one constant-buffer element type into a ReflectedLayout, assigning each resource leaf
		// a synthetic 8-byte slot (where its handle write lands) and its accumulated (group, binding).
		// Scalars are unsupported for now: the compute kernels are buffer-only.
		ReflectedLayout
		ReflectElement(
			slang::TypeLayoutReflection* typeLayout,
			uint32_t                     group,
			uint32_t                     bindingBase,
			uint32_t&                    handleOffset,
			std::vector<BindGroupSlot>&  slots)
		{
			using Kind = slang::TypeReflection::Kind;

			ReflectedLayout result;

			const Kind kind = typeLayout->getKind();

			if (kind == Kind::Resource || kind == Kind::ShaderStorageBuffer)
			{
				result.kind             = UniformType::kValue;
				result.valueType        = UniformValueType::kDescriptorHandle;
				result.size             = 8;
				result.isResourceHandle = true;

				slots.push_back({ group, bindingBase, IsReadWrite(typeLayout->getType()) });
				return result;
			}

			if (kind == Kind::Struct)
			{
				result.kind = UniformType::kStruct;

				const uint32_t fieldCount = typeLayout->getFieldCount();
				for (uint32_t i = 0; i < fieldCount; ++i)
				{
					slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);

					const uint32_t rel = static_cast<uint32_t>(
						field->getOffset(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT));

					ReflectedField reflected;
					reflected.name   = field->getName();
					reflected.offset = handleOffset;
					reflected.layout = ReflectElement(
						field->getTypeLayout(),
						group,
						bindingBase + rel,
						handleOffset,
						slots);

					if (reflected.layout.isResourceHandle)
					{
						reflected.group   = group;
						reflected.binding = bindingBase + rel;
						handleOffset += 8;
					}

					result.fields.push_back(std::move(reflected));
				}

				result.size = handleOffset;
				return result;
			}

			gfatal("ComputePipeline: unsupported reflected kind (compute kernels are buffer-only)");
		}
	}

	ComputePipeline::ComputePipeline(
		const wgpu::Device&        device,
		slang::ISession*           session,
		const ComputePipelineDesc& desc) : m_Desc(desc)
	{
		gassert(m_Desc.shader != nullptr, "ComputePipeline: null shader");

		auto owner   = Slang::ComPtr<slang::IComponentType>();
		auto program = LinkProgram(session, m_Desc.shader.Get(), owner);

		auto slots = std::vector<BindGroupSlot>();

		for (uint32_t i = 0; i < program.layout->getParameterCount(); ++i)
		{
			slang::VariableLayoutReflection* param = program.layout->getParameterByIndex(i);

			// Filter by type, not binding category: a ConstantBuffer whose members hoist to WGSL
			// bindings reflects with a descriptorTableSlot category, not the ConstantBuffer one.
			if (param->getTypeLayout()->getKind() != slang::TypeReflection::Kind::ConstantBuffer)
				continue;

			const uint32_t group   = static_cast<uint32_t>(param->getBindingSpace());
			const uint32_t binding = static_cast<uint32_t>(param->getBindingIndex());

			uint32_t handleOffset = 0;

			auto layout = ReflectElement(
				param->getTypeLayout()->getElementTypeLayout(),
				group,
				binding,
				handleOffset,
				slots);

			UniformLayoutEntry entry;
			entry.size   = handleOffset;
			entry.layout = std::make_shared<const ReflectedLayout>(std::move(layout));

			m_UniformLayoutEntries[param->getName()] = std::move(entry);
		}

		auto entries = std::vector<wgpu::BindGroupLayoutEntry>();
		entries.reserve(slots.size());
		for (const BindGroupSlot& slot : slots)
		{
			gassert(slot.group == 0, "ComputePipeline: only bind group 0 is supported");

			auto entry        = wgpu::BindGroupLayoutEntry{};
			entry.binding     = slot.binding;
			entry.visibility  = wgpu::ShaderStage::Compute;
			entry.buffer.type = slot.readWrite ? wgpu::BufferBindingType::Storage :
			                                     wgpu::BufferBindingType::ReadOnlyStorage;
			entries.push_back(entry);
		}

		auto bglDesc       = wgpu::BindGroupLayoutDescriptor{};
		bglDesc.entryCount = entries.size();
		bglDesc.entries    = entries.data();
		m_BindGroupLayout  = device.CreateBindGroupLayout(&bglDesc);

		auto plDesc                  = wgpu::PipelineLayoutDescriptor{};
		plDesc.bindGroupLayoutCount  = 1;
		wgpu::BindGroupLayout bgls[] = { m_BindGroupLayout };
		plDesc.bindGroupLayouts      = bgls;
		auto pipelineLayout          = device.CreatePipelineLayout(&plDesc);

		auto wgslDesc      = wgpu::ShaderSourceWGSL{};
		wgslDesc.code      = std::string_view(program.wgsl);
		auto smDesc        = wgpu::ShaderModuleDescriptor{};
		smDesc.nextInChain = &wgslDesc;
		auto shaderModule  = device.CreateShaderModule(&smDesc);

		auto cpDesc               = wgpu::ComputePipelineDescriptor{};
		cpDesc.label              = std::string_view(m_Desc.debugName);
		cpDesc.layout             = pipelineLayout;
		cpDesc.compute.module     = shaderModule;
		cpDesc.compute.entryPoint = m_Desc.shader->GetDesc().entryPointName.empty() ?
		                                std::string_view("main") :
		                                std::string_view(m_Desc.shader->GetDesc().entryPointName);

		m_Pipeline = device.CreateComputePipeline(&cpDesc);
	}

	UniformLayoutEntry
	ComputePipeline::GetUniformLayoutEntry(std::string_view name) const noexcept
	{
		auto it = m_UniformLayoutEntries.find(name);
		if (it != m_UniformLayoutEntries.end())
			return it->second;

		gfatal("Uniform layout entry not found: {}", name);
	}

	std::vector<std::string>
	ComputePipeline::GetUniformBufferNames() const noexcept
	{
		auto names = std::vector<std::string>();
		names.reserve(m_UniformLayoutEntries.size());
		for (const auto& [name, entry] : m_UniformLayoutEntries) names.push_back(name);
		return names;
	}
}
