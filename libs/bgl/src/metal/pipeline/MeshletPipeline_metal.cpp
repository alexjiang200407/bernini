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

		// The composed program is used only for reflection (the union of every stage's cbuffers) and
		// the thread-group sizes; the MSL functions are compiled per stage below.
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

		// Compile each stage to its OWN library. A whole-program MSL drops the mesh output's
		// interpolant [[user]] attributes, so any mesh->fragment varying fails to link; per-stage
		// compilation keeps them. (Numbered semantics like TEXCOORD0 still mismatch mesh vs fragment,
		// so interpolant semantics must be un-numbered -- a Slang MSL quirk.) A returned MTL::Function
		// retains its library, so the local Library ptr can drop.
		const auto compileFunction = [&](IShader* shader) -> NS::SharedPtr<MTL::Function> {
			slang::IModule*                   module = shader->GetSlangModule();
			Slang::ComPtr<slang::IEntryPoint> entryPoint;
			module->findEntryPointByName(
				shader->GetDesc().entryPointName.c_str(),
				entryPoint.writeRef());

			slang::IComponentType*               comps[] = { module, entryPoint.get() };
			Slang::ComPtr<slang::IComponentType> prog;
			session->createCompositeComponentType(
				comps,
				2,
				prog.writeRef(),
				errChecker.WriteDiagnosticBlob()) >>
				errChecker;
			Slang::ComPtr<slang::IComponentType> linked;
			prog->link(linked.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

			Slang::ComPtr<slang::IBlob> blob;
			linked->getEntryPointCode(0, 0, blob.writeRef(), errChecker.WriteDiagnosticBlob()) >>
				errChecker;
			std::string entryMsl(
				static_cast<const char*>(blob->getBufferPointer()),
				blob->getBufferSize());

			NS::Error*                  err = nullptr;
			NS::SharedPtr<MTL::Library> lib =
				NS::TransferPtr(device->newLibrary(Str(entryMsl), nullptr, &err));
			if (!lib)
			{
				core::throw_runtime_error(
					"Metal meshlet stage compile failed for '{}': {}",
					shader->GetDesc().debugName,
					err->localizedDescription()->utf8String());
			}
			NS::SharedPtr<MTL::Function> fn =
				NS::TransferPtr(lib->newFunction(Str(shader->GetDesc().entryPointName)));
			gassert(fn.get() != nullptr, "Meshlet stage library is missing its entry function");
			return fn;
		};

		NS::SharedPtr<MTL::Function> meshFn = compileFunction(m_Desc.meshShader.Get());
		NS::SharedPtr<MTL::Function> fragFn = compileFunction(m_Desc.pixelShader.Get());
		NS::SharedPtr<MTL::Function> objFn;
		if (m_Desc.ampShader != nullptr)
			objFn = compileFunction(m_Desc.ampShader.Get());

		NS::Error*                                       error = nullptr;
		NS::SharedPtr<MTL::MeshRenderPipelineDescriptor> pd =
			NS::TransferPtr(MTL::MeshRenderPipelineDescriptor::alloc()->init());
		pd->setMeshFunction(meshFn.get());
		pd->setFragmentFunction(fragFn.get());
		if (objFn)
			pd->setObjectFunction(objFn.get());

		for (size_t i = 0; i < m_Desc.rtvFormats.size(); ++i)
			pd->colorAttachments()->object(i)->setPixelFormat(ConvertFormat(m_Desc.rtvFormats[i]));
		if (m_Desc.dsvFormat != Format::UNKNOWN)
		{
			pd->setDepthAttachmentPixelFormat(ConvertFormat(m_Desc.dsvFormat));

			// Metal bakes the depth test/write into a separate immutable state object bound on the
			// encoder, not into the render PSO (unlike D3D12's DEPTH_STENCIL subobject). A disabled
			// test maps to compare-Always so writes still land when depthWriteEnable is set.
			const DepthStencilState&                   dss = m_Desc.renderState.depthStencilState;
			NS::SharedPtr<MTL::DepthStencilDescriptor> dsd =
				NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
			dsd->setDepthCompareFunction(
				dss.depthTestEnable ? ConvertCompareFunc(dss.depthFunc) :
									  MTL::CompareFunctionAlways);
			dsd->setDepthWriteEnabled(dss.depthWriteEnable);
			m_DepthStencilState = NS::TransferPtr(device->newDepthStencilState(dsd.get()));
		}

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
