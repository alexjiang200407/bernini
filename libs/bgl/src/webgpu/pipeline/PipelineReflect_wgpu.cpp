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

		wgpu::TextureViewDimension
		ViewDimensionForShape(slang::TypeReflection* type) noexcept
		{
			const auto shape = type->getResourceShape();
			const bool array = (shape & SLANG_TEXTURE_ARRAY_FLAG) != 0;

			switch (shape & SLANG_RESOURCE_BASE_SHAPE_MASK)
			{
			case SLANG_TEXTURE_1D:
				return wgpu::TextureViewDimension::e1D;
			case SLANG_TEXTURE_2D:
				return array ? wgpu::TextureViewDimension::e2DArray :
				               wgpu::TextureViewDimension::e2D;
			case SLANG_TEXTURE_3D:
				return wgpu::TextureViewDimension::e3D;
			case SLANG_TEXTURE_CUBE:
				return array ? wgpu::TextureViewDimension::CubeArray :
				               wgpu::TextureViewDimension::Cube;
			default:
				gfatal(
					"wgsl reflect: no view dimension for resource shape {}",
					static_cast<int>(shape));
			}
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

			if (kind == Kind::Resource || kind == Kind::ShaderStorageBuffer ||
			    kind == Kind::SamplerState)
			{
				result.kind            = UniformType::kValue;
				result.valueType       = UniformValueType::kDescriptorHandle;
				result.size            = 8;
				result.resourceBinding = ResolveSlangResourceBinding(typeLayout->getType());

				auto slot = BindGroupSlot{ group, bindingBase };
				slot.type = result.resourceBinding;

				if (slot.type == ResourceBinding::kBuffer)
				{
					slot.bufferType = IsReadWrite(typeLayout->getType()) ?
					                      wgpu::BufferBindingType::Storage :
					                      wgpu::BufferBindingType::ReadOnlyStorage;
				}
				else if (slot.type == ResourceBinding::kTexture)
				{
					slot.viewDimension = ViewDimensionForShape(typeLayout->getType());
				}

				slots.push_back(slot);
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

				const auto     blockSize        = static_cast<uint32_t>(typeLayout->getSize());
				const uint32_t structHandleBase = handleCursor;

				const uint32_t fieldCount = typeLayout->getFieldCount();
				for (uint32_t i = 0; i < fieldCount; ++i)
				{
					slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);

					const uint32_t binding =
						bindingBase + static_cast<uint32_t>(field->getOffset(
										  SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT));

					// Read before recursing, which advances the cursor past this field's handles.
					const uint32_t fieldHandleBase = handleCursor;

					ReflectedField reflected;
					reflected.name   = field->getName();
					reflected.layout = ReflectUniformLayout(
						field->getTypeLayout(),
						group,
						binding,
						depth + 1,
						fieldHandleBase,
						handleCursor,
						slots);

					// A field owning resources sits at the handle allocation instead of in the
					// block, so its subtree's offsets stay relative to it and Traverse sums them
					// back to the same absolute byte.
					if (reflected.layout.resourceBinding != ResourceBinding::kNone)
					{
						reflected.offset  = fieldHandleBase - structOrigin;
						reflected.group   = group;
						reflected.binding = binding;
						handleCursor += reflected.layout.size;
					}
					else if (handleCursor > fieldHandleBase)
					{
						reflected.offset = fieldHandleBase - structOrigin;
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
					depth == 0 || blockSize == 0 || handleCursor == structHandleBase,
					"wgsl reflect: a nested struct may not mix plain data with resources");

				result.size = blockSize + (handleCursor - structHandleBase);
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
			{
				auto uniformSlot       = BindGroupSlot{ group, binding };
				uniformSlot.bufferType = wgpu::BufferBindingType::Uniform;
				slots.push_back(uniformSlot);
			}

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

			// WebGPU forbids a read-write storage binding in the vertex stage outright, so such a
			// slot is offered to the other stages only. A vertex shader that does reach one then
			// fails pipeline creation naming the stage, rather than the whole layout being rejected
			// because some other entry point declared the buffer.
			auto stages = visibility;
			if (slot.type == ResourceBinding::kBuffer &&
			    slot.bufferType == wgpu::BufferBindingType::Storage)
				stages &= ~wgpu::ShaderStage::Vertex;

			auto entry       = wgpu::BindGroupLayoutEntry{};
			entry.binding    = slot.binding;
			entry.visibility = stages;

			switch (slot.type)
			{
			case ResourceBinding::kBuffer:
				entry.buffer.type = slot.bufferType;
				break;
			case ResourceBinding::kTexture:
				// Float rather than UnfilterableFloat: a texture reached this way is one a shader
				// samples, and an unfilterable binding rejects the linear samplers Scene creates.
				entry.texture.sampleType    = wgpu::TextureSampleType::Float;
				entry.texture.viewDimension = slot.viewDimension;
				break;
			case ResourceBinding::kSampler:
				entry.sampler.type = wgpu::SamplerBindingType::Filtering;
				break;
			case ResourceBinding::kNone:
				gfatal("MakeWgslBindGroupLayout: slot at binding {} has no kind", slot.binding);
			}

			entries.push_back(entry);
		}

		auto bglDesc       = wgpu::BindGroupLayoutDescriptor{};
		bglDesc.entryCount = entries.size();
		bglDesc.entries    = entries.data();

		return device.CreateBindGroupLayout(&bglDesc);
	}
}
