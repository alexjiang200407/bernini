#include "pipeline/ComputePipeline_metal.h"

#include "slang/SlangErrorChecker.h"
#include "uniforms/SlangReflection.h"

#include <core/err/util.h>

namespace bgl
{
	namespace
	{
		// Slang's Metal reflection reports sizes/offsets for *ordinary* constant data only, so a bindless
		// handle (a resource, not ordinary bytes) is invisible to getSize()/getOffset() -- a handle-only
		// cbuffer measures 0. But in the emitted MSL each handle is a real 8-byte device pointer. So the
		// constant-buffer byte layout is recomputed here from field sizes/alignments (natural C/MSL
		// rules), independent of the untrustworthy Metal reflection numbers. Returns the type's size.
		uint32_t
		MetalLayoutSize(const ReflectedLayout& layout);

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

		uint32_t
		MetalLayoutSize(const ReflectedLayout& layout)
		{
			switch (layout.kind)
			{
			case UniformType::kValue:
				if (layout.isResourceHandle)
					return 8;
				switch (layout.valueType)
				{
				case UniformValueType::kMat4x4:
					return 64;
				case UniformValueType::kFloat4:
				case UniformValueType::kInt4:
				case UniformValueType::kUInt4:
					return 16;
				case UniformValueType::kFloat3:
				case UniformValueType::kInt3:
				case UniformValueType::kUInt3:
					return 12;
				case UniformValueType::kFloat2:
				case UniformValueType::kInt2:
				case UniformValueType::kUInt2:
					return 8;
				default:
					return 4;
				}
			case UniformType::kArray:
			{
				if (layout.element.empty())
					return 0;
				const uint32_t stride = AlignUp(
					MetalLayoutSize(layout.element.front()),
					MetalAlign(layout.element.front()));
				return stride * layout.arrayCount;
			}
			case UniformType::kStruct:
			default:
				return 0;  // structs are sized by MetalizeStruct
			}
		}

		// Recomputes field offsets and struct sizes for the whole tree under Metal's byte layout.
		// Returns the (aligned) size of `layout`.
		uint32_t
		MetalizeLayout(ReflectedLayout& layout)
		{
			if (layout.kind == UniformType::kStruct)
			{
				uint32_t offset = 0;
				for (ReflectedField& field : layout.fields)
				{
					const uint32_t fieldSize  = MetalizeLayout(field.layout);
					const uint32_t fieldAlign = MetalAlign(field.layout);
					offset                    = AlignUp(offset, fieldAlign);
					field.offset              = offset;
					offset += fieldSize;
				}
				layout.size = AlignUp(offset, MetalAlign(layout));
				return layout.size;
			}

			if (layout.kind == UniformType::kArray && !layout.element.empty())
			{
				MetalizeLayout(layout.element.front());
			}

			layout.size = MetalLayoutSize(layout);
			return layout.size;
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
