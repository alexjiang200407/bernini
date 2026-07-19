#include "pipeline/MeshletPipeline_metal.h"

#include "slang/SlangErrorChecker.h"
#include "util_metal.h"

#include <core/err/util.h>

namespace bgl
{
	namespace
	{
		NS::String*
		Str(const std::string& s)
		{
			return NS::String::string(s.c_str(), NS::UTF8StringEncoding);
		}
	}

	MeshletPipeline::MeshletPipeline(
		MTL::Device*               device,
		slang::ISession*           session,
		const MeshletPipelineDesc& desc) : m_Desc(desc)
	{
		gassert(m_Desc.meshShader != nullptr, "Meshlet pipeline requires a mesh shader");
		gassert(m_Desc.pixelShader != nullptr, "Meshlet pipeline requires a pixel shader");

		SlangErrorChecker errChecker;

		std::vector<slang::IComponentType*>            components;
		std::unordered_set<slang::IModule*>            modules;
		std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints;

		const auto addShader = [&](IShader* shader) {
			if (shader == nullptr)
				return;
			slang::IModule* module = shader->GetSlangModule();
			gassert(module != nullptr, "Shader module cannot be null");
			if (modules.insert(module).second)
				components.push_back(module);

			Slang::ComPtr<slang::IEntryPoint> entryPoint;
			module->findEntryPointByName(
				shader->GetDesc().entryPointName.c_str(),
				entryPoint.writeRef());
			gassert(entryPoint != nullptr, "Failed to find meshlet entry point");
			components.push_back(entryPoint.get());
			entryPoints.push_back(std::move(entryPoint));
		};

		addShader(m_Desc.meshShader.Get());
		addShader(m_Desc.pixelShader.Get());
		addShader(m_Desc.ampShader.Get());

		Slang::ComPtr<slang::IComponentType> program;
		session->createCompositeComponentType(
			components.data(),
			static_cast<SlangInt>(components.size()),
			program.writeRef(),
			errChecker.WriteDiagnosticBlob()) >>
			errChecker;

		Slang::ComPtr<slang::IComponentType> linkedProgram;
		program->link(linkedProgram.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

		// One MSL library holding every entry point (mesh, pixel, and optional object stage).
		Slang::ComPtr<slang::IBlob> code;
		linkedProgram->getTargetCode(0, code.writeRef(), errChecker.WriteDiagnosticBlob()) >>
			errChecker;
		gassert(code != nullptr, "Failed to generate MSL");
		std::string msl(static_cast<const char*>(code->getBufferPointer()), code->getBufferSize());

		slang::ProgramLayout* layout = linkedProgram->getLayout();
		ReflectCbuffers(layout, m_UniformLayoutEntries, m_HandleOffsets);

		const auto threadGroupOf = [&](const std::string& entryName) -> MTL::Size {
			for (SlangUInt i = 0; i < layout->getEntryPointCount(); ++i)
			{
				if (entryName == layout->getEntryPointByIndex(i)->getName())
				{
					SlangUInt tg[3] = { 1, 1, 1 };
					layout->getEntryPointByIndex(i)->getComputeThreadGroupSize(3, tg);
					return MTL::Size(tg[0], tg[1], tg[2]);
				}
			}
			return MTL::Size(1, 1, 1);
		};
		m_ThreadsPerMesh = threadGroupOf(m_Desc.meshShader->GetDesc().entryPointName);
		if (m_Desc.ampShader != nullptr)
			m_ThreadsPerObject = threadGroupOf(m_Desc.ampShader->GetDesc().entryPointName);

		NS::SharedPtr<NS::AutoreleasePool> pool =
			NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::Error*                  error = nullptr;
		NS::SharedPtr<MTL::Library> library =
			NS::TransferPtr(device->newLibrary(Str(msl), nullptr, &error));
		if (!library)
		{
			core::throw_runtime_error(
				"Metal meshlet library compile failed for '{}': {}",
				m_Desc.meshShader->GetDesc().debugName,
				error->localizedDescription()->utf8String());
		}

		// Non-"main" entry names emit verbatim into MSL, so look each function up by its entry name.
		NS::SharedPtr<MTL::Function> meshFn =
			NS::TransferPtr(library->newFunction(Str(m_Desc.meshShader->GetDesc().entryPointName)));
		NS::SharedPtr<MTL::Function> fragFn = NS::TransferPtr(
			library->newFunction(Str(m_Desc.pixelShader->GetDesc().entryPointName)));
		gassert(meshFn && fragFn, "Meshlet library is missing its mesh or fragment function");

		NS::SharedPtr<MTL::Function> objFn;
		if (m_Desc.ampShader != nullptr)
			objFn = NS::TransferPtr(
				library->newFunction(Str(m_Desc.ampShader->GetDesc().entryPointName)));

		NS::SharedPtr<MTL::MeshRenderPipelineDescriptor> pd =
			NS::TransferPtr(MTL::MeshRenderPipelineDescriptor::alloc()->init());
		pd->setMeshFunction(meshFn.get());
		pd->setFragmentFunction(fragFn.get());
		if (objFn)
			pd->setObjectFunction(objFn.get());

		for (size_t i = 0; i < m_Desc.rtvFormats.size(); ++i)
			pd->colorAttachments()->object(i)->setPixelFormat(ConvertFormat(m_Desc.rtvFormats[i]));
		if (m_Desc.dsvFormat != Format::UNKNOWN)
			pd->setDepthAttachmentPixelFormat(ConvertFormat(m_Desc.dsvFormat));

		m_PipelineState = NS::TransferPtr(
			device->newRenderPipelineState(pd.get(), MTL::PipelineOptionNone, nullptr, &error));
		if (!m_PipelineState)
		{
			core::throw_runtime_error(
				"Metal meshlet pipeline failed for '{}': {}",
				m_Desc.meshShader->GetDesc().debugName,
				error->localizedDescription()->utf8String());
		}
	}
}
