#include "pipeline/MeshletPipeline_metal.h"
#include "MetalErrorChecker.h"
#include <core/err/util.h>

#include "convert_metal.h"
#include "pipeline/MetalPipelineReflection.h"
#include "resource/Shader.h"
#include "shadercache/ShaderCache_metal.h"
#include "types/BlendState.h"
#include "types/DepthStencilState.h"
#include "types/Format.h"
#include "types/ShaderStage.h"
#include "uniforms/UniformLayoutEntry.h"
#include "util/util.h"
#include <algorithm>
#include <array>
#include <bgl_common/SlangErrorChecker.h>
#include <bgl_common/gassert.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <slang-com-ptr.h>
#include <slang.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bgl
{

	NS::SharedPtr<MTL::DepthStencilState>
	MeshletPipeline::BuildDepthStencilState(MTL::Device* device) const
	{
		const DepthStencilState& ds       = m_Desc.renderState.depthStencilState;
		const bool               hasDepth = m_Desc.dsvFormat != Format::UNKNOWN;

		NS::SharedPtr<MTL::DepthStencilDescriptor> dsd =
			NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());

		// depthTestEnable gates the write as well: it maps to D3D12's DepthEnable, which turns the
		// whole depth stage off whatever DepthWriteMask says. Metal would otherwise write depth for
		// the default state (test off, write on), which D3D12 leaves untouched. Writing with no depth
		// attachment is separately rejected by Metal, where D3D12 ignores it.
		const bool depthOn = hasDepth && ds.depthTestEnable;
		dsd->setDepthCompareFunction(
			depthOn ? ConvertComparisonFunc(ds.depthFunc) : MTL::CompareFunctionAlways);
		dsd->setDepthWriteEnabled(depthOn && ds.depthWriteEnable);

		if (ds.stencilEnable && GetFormatInfo(m_Desc.dsvFormat).hasStencil)
		{
			const auto face = [&](const DepthStencilState::StencilOpDesc& src) {
				NS::SharedPtr<MTL::StencilDescriptor> sd =
					NS::TransferPtr(MTL::StencilDescriptor::alloc()->init());
				sd->setStencilFailureOperation(ConvertStencilOp(src.failOp));
				sd->setDepthFailureOperation(ConvertStencilOp(src.depthFailOp));
				sd->setDepthStencilPassOperation(ConvertStencilOp(src.passOp));
				sd->setStencilCompareFunction(ConvertComparisonFunc(src.stencilFunc));
				sd->setReadMask(ds.stencilReadMask);
				sd->setWriteMask(ds.stencilWriteMask);
				return sd;
			};
			dsd->setFrontFaceStencil(face(ds.frontFaceStencil).get());
			dsd->setBackFaceStencil(face(ds.backFaceStencil).get());
		}

		auto state = NS::TransferPtr(device->newDepthStencilState(dsd.get()));
		gassert(state.get() != nullptr, "Metal depth-stencil state creation failed");
		return state;
	}

	namespace
	{
		// Compiles the whole PSO through slang into the cacheable form: the union of every stage's
		// cbuffers, and per stage its MSL, its own [[buffer(N)]] indices and its threadgroup size.
		CachedProgram
		CompileProgram(const MeshletPipelineDesc& desc)
		{
			SlangErrorChecker errChecker;

			slang::ISession*                               session = nullptr;
			std::vector<slang::IComponentType*>            components;
			std::unordered_set<slang::IModule*>            modules;
			std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints;

			const auto addShader = [&](IShader* shader) {
				if (shader == nullptr)
					return;
				slang::IModule* module = shader->GetSlangModule();
				gassert(module != nullptr, "Shader module cannot be null");

				// Read off the module so that reaching this function is what creates a session on
				// this thread, and a cache hit never does.
				session = module->getSession();

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

			addShader(desc.meshShader.Get());
			addShader(desc.pixelShader.Get());
			addShader(desc.ampShader.Get());

			Slang::ComPtr<slang::IComponentType> program;
			session->createCompositeComponentType(
				components.data(),
				static_cast<SlangInt>(components.size()),
				program.writeRef(),
				errChecker.WriteDiagnosticBlob()) >>
				errChecker;

			Slang::ComPtr<slang::IComponentType> linkedProgram;
			program->link(linkedProgram.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

			// The composed program is used only for reflection (the union of every stage's cbuffers)
			// and the thread-group sizes; the MSL functions are compiled per stage below.
			slang::ProgramLayout* layout = linkedProgram->getLayout();

			UniformLayoutMap     entries;
			MetalHandleOffsetMap handleOffsets;
			ReflectCbuffers(layout, entries, handleOffsets);

			CachedProgram cached;
			for (const auto& [name, entry] : entries)
			{
				CachedCbuffer cbuffer;
				cbuffer.name    = name;
				cbuffer.size    = entry.size;
				cbuffer.layout  = *entry.layout;
				cbuffer.handles = handleOffsets[name];
				cached.cbuffers.push_back(std::move(cbuffer));
			}

			const auto threadGroupOf =
				[&](const std::string& entryName) -> std::array<uint32_t, 3> {
				for (SlangUInt i = 0; i < layout->getEntryPointCount(); ++i)
				{
					if (entryName == layout->getEntryPointByIndex(i)->getName())
					{
						SlangUInt tg[3] = { 1, 1, 1 };
						layout->getEntryPointByIndex(i)->getComputeThreadGroupSize(3, tg);
						return { static_cast<uint32_t>(tg[0]),
							     static_cast<uint32_t>(tg[1]),
							     static_cast<uint32_t>(tg[2]) };
					}
				}
				return { 1, 1, 1 };
			};

			// A mesh-only pipeline exists for its reflection, not to draw -- CreateUniforms builds
			// from one. It gets no MSL and no PSO: nothing can rasterize without a fragment function,
			// and Slang's MSL backend segfaults generating code for a mesh entry with no mesh output,
			// which is exactly the shape a reflection-only shader has. GetMTLPipelineState() is null,
			// and DispatchMesh refuses it. Its mesh stage is still recorded, for the threadgroup size.
			if (desc.pixelShader == nullptr)
			{
				CachedStage stage;
				stage.stage                 = ShaderStage::kMesh;
				stage.entryPoint            = desc.meshShader->GetDesc().entryPointName;
				stage.threadsPerThreadgroup = threadGroupOf(stage.entryPoint);
				cached.stages.push_back(std::move(stage));
				return cached;
			}

			// Compile each stage to its OWN program. A whole-program MSL drops the mesh output's
			// interpolant [[user]] attributes, so any mesh->fragment varying fails to link; per-stage
			// compilation keeps them. (Numbered semantics like TEXCOORD0 still mismatch mesh vs
			// fragment, so interpolant semantics must be un-numbered -- a Slang MSL quirk.) Because
			// each stage is its own program, its [[buffer(N)]] indices are its own too.
			const auto compileStage = [&](IShader* shader, ShaderStage stageKind) {
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

				MetalStageBindingMap bindings;
				ReflectStageBindings(linked->getLayout(), bindings);

				Slang::ComPtr<slang::IBlob> blob;
				linked->getEntryPointCode(
					0,
					0,
					blob.writeRef(),
					errChecker.WriteDiagnosticBlob()) >>
					errChecker;

				CachedStage stage;
				stage.stage      = stageKind;
				stage.entryPoint = shader->GetDesc().entryPointName;
				stage.msl        = std::string(
					static_cast<const char*>(blob->getBufferPointer()),
					blob->getBufferSize());
				for (const auto& [name, index] : bindings) stage.bindings.emplace_back(name, index);
				stage.threadsPerThreadgroup = threadGroupOf(stage.entryPoint);

				cached.stages.push_back(std::move(stage));
			};

			compileStage(desc.meshShader.Get(), ShaderStage::kMesh);
			compileStage(desc.pixelShader.Get(), ShaderStage::kPixel);
			if (desc.ampShader != nullptr)
				compileStage(desc.ampShader.Get(), ShaderStage::kAmplification);

			return cached;
		}

	}

	MeshletPipeline::MeshletPipeline(
		MTL::Device*               device,
		ShaderCache*               shaderCache,
		const MeshletPipelineDesc& desc) : m_Desc(desc)
	{
		gassert(m_Desc.meshShader != nullptr, "Meshlet pipeline requires a mesh shader");

		uint64_t      key = 0;
		CachedProgram cached;
		bool          hit = false;
		if (shaderCache != nullptr)
		{
			std::vector<std::pair<std::string, std::string>> moduleEntries;
			for (const IShader* shader :
			     { m_Desc.meshShader.Get(), m_Desc.pixelShader.Get(), m_Desc.ampShader.Get() })
			{
				if (shader != nullptr)
				{
					moduleEntries.emplace_back(
						shader->GetDesc().slangModuleName,
						shader->GetDesc().entryPointName);
				}
			}
			key = shaderCache->ComputeKey(std::move(moduleEntries));
			hit = shaderCache->TryLoad(key, cached);
		}

		if (!hit)
		{
			cached = CompileProgram(m_Desc);
			if (shaderCache != nullptr)
				shaderCache->Store(key, cached);
		}

		for (const CachedCbuffer& cbuffer : cached.cbuffers)
		{
			UniformLayoutEntry entry{};
			entry.size   = cbuffer.size;
			entry.layout = std::make_shared<const ReflectedLayout>(cbuffer.layout);
			m_UniformLayoutEntries[cbuffer.name] = std::move(entry);
			m_HandleOffsets[cbuffer.name]        = cbuffer.handles;
		}

		for (const CachedStage& stage : cached.stages)
		{
			MetalStageBindingMap& bindings = m_StageBindings[static_cast<size_t>(stage.stage)];
			for (const auto& [name, index] : stage.bindings) bindings[name] = index;

			const MTL::Size threads = MTL::Size(
				stage.threadsPerThreadgroup[0],
				stage.threadsPerThreadgroup[1],
				stage.threadsPerThreadgroup[2]);
			if (stage.stage == ShaderStage::kMesh)
				m_ThreadsPerMesh = threads;
			else if (stage.stage == ShaderStage::kAmplification)
				m_ThreadsPerObject = threads;
		}

		// A mesh-only pipeline carries no fragment stage, so there is nothing to rasterize with and
		// no PSO to build -- see CompileProgram.
		const bool hasFragment = std::ranges::any_of(cached.stages, [](const CachedStage& stage) {
			return stage.stage == ShaderStage::kPixel;
		});
		if (!hasFragment)
			return;

		NS::SharedPtr<NS::AutoreleasePool> pool =
			NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		// A returned MTL::Function retains its library, so the local Library ptr can drop.
		const auto makeFunction = [&](const CachedStage& stage) -> NS::SharedPtr<MTL::Function> {
			MetalErrorChecker           errChecker;
			NS::SharedPtr<MTL::Library> lib = NS::TransferPtr(
				device->newLibrary(ConvertString(stage.msl), nullptr, errChecker.WriteError()));
			lib.get() >> errChecker;
			NS::SharedPtr<MTL::Function> fn =
				NS::TransferPtr(lib->newFunction(ConvertString(stage.entryPoint)));
			gassert(fn.get() != nullptr, "Meshlet stage library is missing its entry function");
			return fn;
		};

		NS::SharedPtr<MTL::Function> meshFn;
		NS::SharedPtr<MTL::Function> fragFn;
		NS::SharedPtr<MTL::Function> objFn;
		for (const CachedStage& stage : cached.stages)
		{
			switch (stage.stage)
			{
			case ShaderStage::kMesh:
				meshFn = makeFunction(stage);
				break;
			case ShaderStage::kPixel:
				fragFn = makeFunction(stage);
				break;
			case ShaderStage::kAmplification:
				objFn = makeFunction(stage);
				break;
			case ShaderStage::kCompute:
			case ShaderStage::kCount:
				gfatal("A meshlet program cannot carry a compute stage");
			}
		}

		NS::SharedPtr<MTL::MeshRenderPipelineDescriptor> pd =
			NS::TransferPtr(MTL::MeshRenderPipelineDescriptor::alloc()->init());
		pd->setMeshFunction(meshFn.get());
		pd->setFragmentFunction(fragFn.get());
		if (objFn)
			pd->setObjectFunction(objFn.get());

		MetalErrorChecker errChecker;

		const BlendState& blend = m_Desc.renderState.blendState;
		for (size_t i = 0; i < m_Desc.rtvFormats.size(); ++i)
		{
			MTL::RenderPipelineColorAttachmentDescriptor* c = pd->colorAttachments()->object(i);
			c->setPixelFormat(ConvertFormat(m_Desc.rtvFormats[i]));

			const BlendState::RenderTarget& t = blend.targets[i];
			c->setWriteMask(ConvertColorWriteMask(t.colorWriteMask));
			c->setBlendingEnabled(t.blendEnable);
			if (!t.blendEnable)
				continue;

			c->setSourceRGBBlendFactor(ConvertBlendFactor(t.srcBlend));
			c->setDestinationRGBBlendFactor(ConvertBlendFactor(t.destBlend));
			c->setRgbBlendOperation(ConvertBlendOp(t.blendOp));
			c->setSourceAlphaBlendFactor(ConvertBlendFactor(t.srcBlendAlpha));
			c->setDestinationAlphaBlendFactor(ConvertBlendFactor(t.destBlendAlpha));
			c->setAlphaBlendOperation(ConvertBlendOp(t.blendOpAlpha));
		}
		pd->setAlphaToCoverageEnabled(blend.alphaToCoverageEnable);

		if (m_Desc.dsvFormat != Format::UNKNOWN)
		{
			pd->setDepthAttachmentPixelFormat(ConvertFormat(m_Desc.dsvFormat));
			if (GetFormatInfo(m_Desc.dsvFormat).hasStencil)
				pd->setStencilAttachmentPixelFormat(ConvertFormat(m_Desc.dsvFormat));
		}

		m_DepthStencilState = BuildDepthStencilState(device);

		const auto create = [&](MTL::BinaryArchive* archive) {
			if (archive != nullptr)
			{
				const MTL::BinaryArchive* archives[] = { archive };
				pd->setBinaryArchives(
					NS::Array::array(
						reinterpret_cast<const NS::Object* const*>(archives),
						std::size(archives)));
			}

			m_PipelineState = NS::TransferPtr(device->newRenderPipelineState(
				pd.get(),
				MTL::PipelineOptionNone,
				nullptr,
				errChecker.WriteError()));
			m_PipelineState.get() >> errChecker;

			// Adding after creation: the descriptor only reads from an archive, and a pipeline the
			// archive already holds is added again as a no-op rather than an error.
			if (archive != nullptr && archive->addMeshRenderPipelineFunctions(pd.get(), nullptr))
				shaderCache->MarkArchiveDirty();
		};

		if (shaderCache != nullptr)
			shaderCache->WithArchive(create);
		else
			create(nullptr);
	}
}
