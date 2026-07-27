#include "pipeline/PipelineReflect_wgpu.h"

#include "slang/SlangErrorChecker.h"
#include "uniforms/SlangReflection.h"

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

		// Reflects a cbuffer element. Plain-data leaves keep Slang's std140 offsets so the block
		// uploads verbatim; resource leaves become 8-byte kDescriptorHandles drawn from
		// handleCursor, which starts past the block. Both are struct-relative, which
		// Uniforms::Traverse and the dispatch/draw path accumulate down the tree.
		ReflectedLayout
		ReflectUniformLayout(
			slang::TypeLayoutReflection* typeLayout,
			uint32_t                     group,
			uint32_t                     bindingBase,
			uint32_t                     depth,
			uint32_t                     structOrigin,
			uint32_t&                    handleCursor,
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

				const auto type = IsReadWrite(typeLayout->getType()) ?
				                      wgpu::BufferBindingType::Storage :
				                      wgpu::BufferBindingType::ReadOnlyStorage;
				slots.push_back({ group, bindingBase, type });
				return result;
			}

			if (kind == Kind::Scalar || kind == Kind::Vector || kind == Kind::Matrix)
			{
				result.kind      = UniformType::kValue;
				result.valueType = ResolveSlangValueType(typeLayout->getType());
				result.size      = static_cast<uint32_t>(typeLayout->getSize());
				return result;
			}

			if (kind == Kind::Struct)
			{
				result.kind = UniformType::kStruct;

				const auto     blockSize  = static_cast<uint32_t>(typeLayout->getSize());
				const uint32_t handleBase = handleCursor;

				const uint32_t fieldCount = typeLayout->getFieldCount();
				for (uint32_t i = 0; i < fieldCount; ++i)
				{
					slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);

					const uint32_t binding =
						bindingBase + static_cast<uint32_t>(field->getOffset(
										  SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT));

					// A field owning handles is placed where the cursor stands, so the offsets its
					// subtree hands out stay relative to it -- Traverse sums them back to the same
					// absolute byte. Captured before recursing, since the subtree moves the cursor.
					const uint32_t fieldStart = handleCursor;

					ReflectedField reflected;
					reflected.name   = field->getName();
					reflected.layout = ReflectUniformLayout(
						field->getTypeLayout(),
						group,
						binding,
						depth + 1,
						fieldStart,
						handleCursor,
						slots);

					if (reflected.layout.isResourceHandle)
					{
						reflected.offset  = fieldStart - structOrigin;
						reflected.group   = group;
						reflected.binding = binding;
						handleCursor += reflected.layout.size;
					}
					else if (handleCursor > fieldStart)
					{
						reflected.offset = fieldStart - structOrigin;
					}
					else
					{
						reflected.offset = static_cast<uint32_t>(
							field->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM));
					}

					result.fields.push_back(std::move(reflected));
				}

				// Only the root can hold both regions: its handles start past its block, while a
				// nested struct would have to place its own block and its handles at one offset.
				gassert(
					depth == 0 || blockSize == 0 || handleCursor == handleBase,
					"wgsl reflect: a nested struct may not mix plain data with resources");

				result.size = blockSize + (handleCursor - handleBase);
				return result;
			}

			gfatal("wgsl reflect: unsupported reflected kind");
		}
	}

	slang::ProgramLayout*
	LinkWgslProgram(
		slang::ISession*                      session,
		std::span<const WgslEntryPoint>       entryPoints,
		Slang::ComPtr<slang::IComponentType>& owner)
	{
		SlangErrorChecker errChecker;

		// A module is composed once even when it supplies several entry points; Slang rejects a
		// duplicate component. Order does not matter, so a linear scan over the few modules is fine.
		auto components = std::vector<slang::IComponentType*>();
		auto modules    = std::vector<slang::IModule*>();
		auto handles    = std::vector<Slang::ComPtr<slang::IEntryPoint>>();
		handles.reserve(entryPoints.size());

		for (const WgslEntryPoint& entry : entryPoints)
		{
			gassert(entry.module != nullptr, "LinkWgslProgram: null shader module");

			if (std::ranges::find(modules, entry.module) == modules.end())
			{
				modules.push_back(entry.module);
				components.push_back(entry.module);
			}

			auto handle = Slang::ComPtr<slang::IEntryPoint>();
			entry.module->findEntryPointByName(entry.name.c_str(), handle.writeRef());
			if (handle == nullptr)
				throw GraphicsError("wgsl: entry point not found: " + entry.name);

			components.push_back(handle.get());
			handles.push_back(std::move(handle));
		}

		auto program = Slang::ComPtr<slang::IComponentType>();
		session->createCompositeComponentType(
			components.data(),
			static_cast<SlangInt>(components.size()),
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

			slang::TypeLayoutReflection* element = param->getTypeLayout()->getElementTypeLayout();

			// Slang gathers the plain-data members into a uniform buffer at the constant buffer's
			// own binding and starts the resource slots after it, so the handle table begins where
			// that block ends. A resource-only buffer has no block and no uniform slot.
			const auto blockSize = static_cast<uint32_t>(element->getSize());
			if (blockSize > 0)
				slots.push_back({ group, binding, wgpu::BufferBindingType::Uniform });

			uint32_t handleCursor = blockSize;

			auto reflected =
				ReflectUniformLayout(element, group, binding, 0, 0, handleCursor, slots);

			UniformLayoutEntry entry;
			entry.size             = handleCursor;
			entry.uniformBlockSize = blockSize;
			entry.uniformBinding   = blockSize > 0 ? binding : 0xFFFFFFFF;
			entry.layout           = std::make_shared<const ReflectedLayout>(std::move(reflected));

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
			entry.buffer.type = slot.type;
			entries.push_back(entry);
		}

		auto bglDesc       = wgpu::BindGroupLayoutDescriptor{};
		bglDesc.entryCount = entries.size();
		bglDesc.entries    = entries.data();

		return device.CreateBindGroupLayout(&bglDesc);
	}
}
