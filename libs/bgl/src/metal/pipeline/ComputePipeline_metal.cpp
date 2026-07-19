#include "pipeline/ComputePipeline_metal.h"

#include "slang/SlangErrorChecker.h"
#include "uniforms/SlangReflection.h"
#include "uniforms/Uniforms.h"  // detail::ValueTypeSize

#include <core/err/util.h>

namespace bgl
{
	namespace
	{
		uint32_t
		MetalAlign(const ReflectedLayout& layout)
		{
			if (layout.isResourceHandle)
				return 8;  // a device pointer / resource id

			switch (layout.kind)
			{
			case UniformType::kStruct:
			{
				uint32_t align = 1;
				for (const ReflectedField& field : layout.fields)
					align = std::max(align, MetalAlign(field.layout));
				return align;
			}
			case UniformType::kArray:
				return layout.element.empty() ? 1 : MetalAlign(layout.element.front());
			case UniformType::kValue:
				switch (layout.valueType)
				{
				case UniformValueType::kFloat3:
				case UniformValueType::kFloat4:
				case UniformValueType::kInt3:
				case UniformValueType::kInt4:
				case UniformValueType::kUInt3:
				case UniformValueType::kUInt4:
				case UniformValueType::kMat4x4:
					return 16;
				case UniformValueType::kFloat2:
				case UniformValueType::kInt2:
				case UniformValueType::kUInt2:  // == kDescriptorHandle
					return 8;
				default:
					return 4;
				}
			default:
				return 1;
			}
		}

		uint32_t
		AlignUp(uint32_t offset, uint32_t align)
		{
			return (offset + align - 1) & ~(align - 1);
		}

		// The byte offset of every bindless handle field within the cbuffer. Precomputed once so the
		// dispatch-time gpuAddress translation doesn't re-walk the layout each call.
		void
		CollectHandleOffsets(
			const ReflectedLayout& layout,
			uint32_t               base,
			std::vector<uint32_t>& out)
		{
			switch (layout.kind)
			{
			case UniformType::kValue:
				if (layout.isResourceHandle)
					out.push_back(base);
				break;
			case UniformType::kStruct:
				for (const ReflectedField& field : layout.fields)
					CollectHandleOffsets(field.layout, base + field.offset, out);
				break;
			case UniformType::kArray:
				if (!layout.element.empty())
					for (uint32_t i = 0; i < layout.arrayCount; ++i)
						CollectHandleOffsets(
							layout.element.front(),
							base + i * layout.arrayStride,
							out);
				break;
			default:
				break;
			}
		}

		// Recomputes the whole tree's field offsets, leaf data sizes, and array strides under Metal's
		// cbuffer layout, and returns the type's *footprint* (its slot size). Metal reflection can't
		// supply these: a bindless handle is a resource, invisible to the ordinary-data category, so a
		// handle-bearing cbuffer measures 0 there while the emitted MSL lays each handle out as an
		// 8-byte device pointer. `size` on a leaf stays the data size the CPU writes (a cbuffer float3
		// is 12 bytes of data), which is smaller than its 16-byte footprint.
		uint32_t
		MetalizeLayout(ReflectedLayout& layout)
		{
			switch (layout.kind)
			{
			case UniformType::kValue:
				layout.size = layout.isResourceHandle ?
				                  8u :
				                  static_cast<uint32_t>(detail::ValueTypeSize(layout.valueType));
				return AlignUp(layout.size, MetalAlign(layout));

			case UniformType::kArray:
			{
				if (layout.element.empty())
					return 0;
				const uint32_t elementFootprint = MetalizeLayout(layout.element.front());
				layout.arrayStride              = elementFootprint;
				layout.size                     = elementFootprint * layout.arrayCount;
				return layout.size;
			}

			case UniformType::kStruct:
			{
				uint32_t offset = 0;
				for (ReflectedField& field : layout.fields)
				{
					const uint32_t footprint = MetalizeLayout(field.layout);
					offset                   = AlignUp(offset, MetalAlign(field.layout));
					field.offset             = offset;
					offset += footprint;
				}
				layout.size = AlignUp(offset, MetalAlign(layout));
				return layout.size;
			}

			default:
				layout.size = 0;
				return 0;
			}
		}
	}

	ComputePipeline::ComputePipeline(
		MTL::Device*               device,
		slang::ISession*           session,
		const ComputePipelineDesc& desc) : m_Desc(desc)
	{
		gassert(m_Desc.shader != nullptr, "Compute pipeline requires a shader");

		IShader*           shader    = m_Desc.shader.Get();
		const std::string& entryName = shader->GetDesc().entryPointName;

		SlangErrorChecker errChecker;

		slang::IModule* module = shader->GetSlangModule();
		gassert(module != nullptr, "Shader module cannot be null");

		Slang::ComPtr<slang::IEntryPoint> entryPoint;
		module->findEntryPointByName(entryName.c_str(), entryPoint.writeRef());
		gassert(entryPoint != nullptr, "Failed to find entry point in module");

		slang::IComponentType* components[] = { module, entryPoint.get() };

		Slang::ComPtr<slang::IComponentType> program;
		session->createCompositeComponentType(
			components,
			2,
			program.writeRef(),
			errChecker.WriteDiagnosticBlob()) >>
			errChecker;

		Slang::ComPtr<slang::IComponentType> linkedProgram;
		program->link(linkedProgram.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

		Slang::ComPtr<slang::IBlob> code;
		linkedProgram->getEntryPointCode(0, 0, code.writeRef(), errChecker.WriteDiagnosticBlob()) >>
			errChecker;
		gassert(code != nullptr, "Failed to generate MSL");

		std::string msl(static_cast<const char*>(code->getBufferPointer()), code->getBufferSize());

		slang::ProgramLayout* layout = linkedProgram->getLayout();

		for (uint32_t i = 0; i < layout->getParameterCount(); ++i)
		{
			slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);
			if (param->getCategory() != slang::ParameterCategory::ConstantBuffer)
				continue;

			slang::TypeLayoutReflection* elementLayout =
				param->getTypeLayout()->getElementTypeLayout();

			// Metal reflection can't be trusted for byte offsets/sizes (handles are invisible to the
			// ordinary-data category), so recompute the layout from the field types.
			ReflectedLayout reflected = ReflectLayoutFromSlang(elementLayout);
			const uint32_t  size      = MetalizeLayout(reflected);

			std::vector<uint32_t> handleOffsets;
			CollectHandleOffsets(reflected, 0, handleOffsets);
			m_HandleOffsets[param->getName()] = std::move(handleOffsets);

			UniformLayoutEntry entry{};
			entry.size   = size;
			entry.layout = std::make_shared<const ReflectedLayout>(std::move(reflected));
			// On Metal the binding index is the [[buffer(N)]] slot the kernel reads uniforms from.
			entry.rootParamIndex = static_cast<uint32_t>(param->getBindingIndex());
			m_UniformLayoutEntries[param->getName()] = std::move(entry);
		}

		SlangUInt threadGroup[3] = { 1, 1, 1 };
		layout->getEntryPointByIndex(0)->getComputeThreadGroupSize(3, threadGroup);
		m_ThreadsPerThreadgroup = MTL::Size(threadGroup[0], threadGroup[1], threadGroup[2]);

		NS::SharedPtr<NS::AutoreleasePool> pool =
			NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::Error*                  error   = nullptr;
		NS::SharedPtr<MTL::Library> library = NS::TransferPtr(device->newLibrary(
			NS::String::string(msl.c_str(), NS::UTF8StringEncoding),
			nullptr,
			&error));
		if (!library)
		{
			core::throw_runtime_error(
				"Metal library compile failed for '{}': {}",
				m_Desc.debugName,
				error->localizedDescription()->utf8String());
		}

		// Slang mangles the entry name in MSL (main -> main_0); a single-entry compute library exposes
		// exactly one kernel function, so take it by name rather than guessing the mangled form.
		NS::Array* names = library->functionNames();
		gassert(names->count() == 1, "Compute library must expose exactly one kernel function");
		NS::SharedPtr<MTL::Function> fn =
			NS::TransferPtr(library->newFunction(static_cast<NS::String*>(names->object(0))));

		m_PipelineState = NS::TransferPtr(device->newComputePipelineState(fn.get(), &error));
		if (!m_PipelineState)
		{
			core::throw_runtime_error(
				"Metal compute pipeline failed for '{}': {}",
				m_Desc.debugName,
				error->localizedDescription()->utf8String());
		}
	}
}
