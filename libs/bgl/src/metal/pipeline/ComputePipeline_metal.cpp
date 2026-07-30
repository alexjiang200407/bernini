#include "pipeline/ComputePipeline_metal.h"

#include "pipeline/MetalPipelineReflection.h"
#include "slang/SlangErrorChecker.h"

#include <core/err/util.h>

namespace bgl
{
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
		ReflectCbuffers(layout, m_UniformLayoutEntries, m_HandleOffsets);

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
