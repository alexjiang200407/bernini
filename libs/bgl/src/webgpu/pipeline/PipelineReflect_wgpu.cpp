#include "pipeline/PipelineReflect_wgpu.h"

#include "slang/SlangErrorChecker.h"

#include <bgl/IGraphics.h>

namespace bgl
{
	namespace
	{
		bool
		IsReadWrite(slang::TypeReflection* type) noexcept
		{
			return type->getResourceAccess() == SLANG_RESOURCE_ACCESS_READ_WRITE;
		}

		// Reflects a cbuffer element into a handle table: each resource leaf becomes an 8-byte
		// kDescriptorHandle at a struct-relative offset, tagged with its (group, binding). Offsets
		// must be struct-relative -- Uniforms::Traverse and the dispatch/draw path accumulate down
		// the tree.
		ReflectedLayout
		ReflectHandleLayout(
			slang::TypeLayoutReflection* typeLayout,
			uint32_t                     group,
			uint32_t                     bindingBase,
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

				uint32_t offset = 0;

				const uint32_t fieldCount = typeLayout->getFieldCount();
				for (uint32_t i = 0; i < fieldCount; ++i)
				{
					slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);

					const uint32_t binding =
						bindingBase + static_cast<uint32_t>(field->getOffset(
										  SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT));

					ReflectedField reflected;
					reflected.name   = field->getName();
					reflected.offset = offset;
					reflected.layout =
						ReflectHandleLayout(field->getTypeLayout(), group, binding, slots);

					if (reflected.layout.isResourceHandle)
					{
						reflected.group   = group;
						reflected.binding = binding;
					}

					offset += reflected.layout.size;
					result.fields.push_back(std::move(reflected));
				}

				result.size = offset;
				return result;
			}

			gfatal("wgsl reflect: unsupported reflected kind (buffers only for now)");
		}
	}

	slang::ProgramLayout*
	LinkWgslProgram(
		slang::ISession*                      session,
		slang::IModule*                       module,
		std::span<const std::string>          entryPointNames,
		Slang::ComPtr<slang::IComponentType>& owner)
	{
		SlangErrorChecker errChecker;

		gassert(module != nullptr, "LinkWgslProgram: null shader module");

		auto components = std::vector<slang::IComponentType*>();
		components.push_back(module);

		auto entryPoints = std::vector<Slang::ComPtr<slang::IEntryPoint>>();
		entryPoints.reserve(entryPointNames.size());

		for (const std::string& name : entryPointNames)
		{
			auto entryPoint = Slang::ComPtr<slang::IEntryPoint>();
			module->findEntryPointByName(name.c_str(), entryPoint.writeRef());
			if (entryPoint == nullptr)
				throw GraphicsError("wgsl: entry point not found: " + name);

			components.push_back(entryPoint.get());
			entryPoints.push_back(std::move(entryPoint));
		}

		auto program = Slang::ComPtr<slang::IComponentType>();
		session->createCompositeComponentType(
			components.data(),
			components.size(),
			program.writeRef(),
			errChecker.WriteDiagnosticBlob()) >>
			errChecker;

		program->link(owner.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

		return owner->getLayout();
	}

	void
	ReflectWgslBindings(
		slang::ProgramLayout*       layout,
		UniformLayoutMap&           entries,
		std::vector<BindGroupSlot>& slots)
	{
		for (uint32_t i = 0; i < layout->getParameterCount(); ++i)
		{
			slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);

			// Filter by type, not binding category: a ConstantBuffer whose members hoist to WGSL
			// bindings reflects with a descriptorTableSlot category, not the ConstantBuffer one.
			if (param->getTypeLayout()->getKind() != slang::TypeReflection::Kind::ConstantBuffer)
				continue;

			const uint32_t group   = static_cast<uint32_t>(param->getBindingSpace());
			const uint32_t binding = static_cast<uint32_t>(param->getBindingIndex());

			auto reflected = ReflectHandleLayout(
				param->getTypeLayout()->getElementTypeLayout(),
				group,
				binding,
				slots);

			UniformLayoutEntry entry;
			entry.size   = reflected.size;
			entry.layout = std::make_shared<const ReflectedLayout>(std::move(reflected));

			entries[param->getName()] = std::move(entry);
		}
	}

	wgpu::BindGroupLayout
	MakeWgslBindGroupLayout(
		const wgpu::Device&            device,
		std::span<const BindGroupSlot> slots,
		wgpu::ShaderStage              visibility)
	{
		auto entries = std::vector<wgpu::BindGroupLayoutEntry>();
		entries.reserve(slots.size());

		for (const BindGroupSlot& slot : slots)
		{
			gassert(slot.group == 0, "MakeWgslBindGroupLayout: only bind group 0 is supported");

			auto entry        = wgpu::BindGroupLayoutEntry{};
			entry.binding     = slot.binding;
			entry.visibility  = visibility;
			entry.buffer.type = slot.readWrite ? wgpu::BufferBindingType::Storage :
			                                     wgpu::BufferBindingType::ReadOnlyStorage;
			entries.push_back(entry);
		}

		auto bglDesc       = wgpu::BindGroupLayoutDescriptor{};
		bglDesc.entryCount = entries.size();
		bglDesc.entries    = entries.data();

		return device.CreateBindGroupLayout(&bglDesc);
	}
}
