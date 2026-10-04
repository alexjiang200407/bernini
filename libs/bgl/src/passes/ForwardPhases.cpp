#include "passes/ForwardPhases.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/frame_constants.h"
#include "passes/BindingNameCheck.h"
#include "passes/DrawData.h"
#include "passes/SceneBindings.h"
#include "passes/draw_bucket_config.h"
#include "scene/Scene.h"
#include "scene/scene_buffer_names.h"
#include "util/util.h"
#include <array>
#include <bgl/ISceneView.h>
#include <bgl/idl/BaseTable.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/constants/constants.h>
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/pipeline/PipelineBatch.h>
#include <bgpu/resource/FrameBuffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/BlendState.h>
#include <bgpu/types/DepthStencilState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/RasterState.h>
#include <bgpu/types/RenderState.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		// Every member BindKernel and its callers name, beyond the buffer tables above. Kept beside
		// the code that writes them so BindingNameCheck catches a shader rename at startup: a
		// stale name is indistinguishable from an absent one once binding reaches IsValid().
		constexpr std::array<std::string_view, 6> c_ViewDataFields = {
			"viewProj"sv, "prevViewProj"sv, "jitter"sv, "prevJitter"sv, "time"sv, "prevTime"sv,
		};

		// clang-format off
		constexpr std::array<std::string_view, 12> c_MaterialDataFields = {
			"anisoLinearWrapSampler"sv,
			"linearClampSampler"sv,
			"irradianceMap"sv,
			"prefilterMap"sv,
			"brdfLUT"sv,
			"cameraPos"sv,
			"exposure"sv,
			"envRotation"sv,
			"alphaHashSeed"sv,
			"sunDirection"sv,
			"sunRadiance"sv,
			"toonShadingRigBlocks"sv,
		};
		// clang-format on

		constexpr std::array<std::string_view, 5> c_ExpansionDataFields = {
			"drawLane"sv, "baseTable"sv, "compactedInstances"sv, "cullBackfaces"sv, "lodDrawMode"sv,
		};

		constexpr auto c_SceneColorFormat = bgpu::Format::RGBA16_FLOAT;

		// The shared blend kernel's programs: the whole depth-sorted list draws through this one
		// pipeline, and AnyMesh branches tier per instance, so no bucket needs a blend kernel of
		// its own.
		constexpr auto c_AnyGeomSrc     = "programs.forward.AnyMesh"sv;
		constexpr auto c_TransparentSrc = "programs.forward.Transparent"sv;

		struct PsoConfig
		{
			std::string          pixelSrc;
			bgpu::RasterCullMode cull;
			bool                 depthWrite;
			bool                 blend;
			bgpu::ComparisonFunc depthFunc = bgpu::ComparisonFunc::kLess;
			std::string_view     geomSrc;
			// The dissolve lane's entries: MSDissolve and PSDissolve, which carry and read the
			// placement's dissolve code, beside the at-rest lane's MSMain and PSMain.
			std::string_view meshEntry  = "MSMain"sv;
			std::string_view pixelEntry = "PSMain"sv;
		};

		// Every bucket kernel is opaque-shaped; only the shared blend kernel differs.
		// A toon character's bucket at rest draws through MSToon, whose vertices carry the placement's
		// toon shading rig block; its dissolve lane, and every other bucket, through the shared ones.
		PsoConfig
		ConfigFor(const DrawBucketDesc& desc, const DrawLane lane, const bool toonCharacter)
		{
			auto config =
				PsoConfig{ DrawBucketPixelSrc(desc),    DrawBucketCullMode(desc),   true, false,
				           bgpu::ComparisonFunc::kLess, DrawBucketGeometrySrc(desc) };
			if (lane == DrawLane::kDissolve)
			{
				config.meshEntry  = "MSDissolve"sv;
				config.pixelEntry = "PSDissolve"sv;
			}
			else if (toonCharacter && desc.geom != GeometryStage::kGrass)
			{
				config.meshEntry = "MSToon"sv;
			}
			return config;
		}

		bgpu::MeshletPipelineDesc
		ForwardPipelineDesc(bgpu::IDevice* device, const PsoConfig& cfg)
		{
			auto pipelineDesc = bgpu::MeshletPipelineDesc();

			pipelineDesc.ampShader = device->CreateShader(std::string(cfg.geomSrc), "ASMain");
			pipelineDesc.meshShader =
				device->CreateShader(std::string(cfg.geomSrc), std::string(cfg.meshEntry));

			pipelineDesc.pixelShader =
				device->CreateShader(std::string(cfg.pixelSrc), std::string(cfg.pixelEntry));

			pipelineDesc.AddRtvFormat(c_SceneColorFormat);

			// The rtvFormats count is what the bound framebuffer must match, so a blend PSO omitting
			// this is also what keeps the velocity buffer out of its attachments.
			if (!cfg.blend)
			{
				pipelineDesc.AddRtvFormat(c_MotionVectorFormat);
			}
			pipelineDesc.SetDsvFormat(bgpu::Format::D24S8);

			auto raster = bgpu::RasterState();
			raster.SetFillMode(bgpu::RasterFillMode::kSolid)
				.SetCullMode(cfg.cull)
				.SetFrontCounterClockwise(true)
				.SetDepthClipEnable(true);

			auto depth = bgpu::DepthStencilState{};
			depth.SetDepthTestEnable(true)
				.SetDepthWriteEnable(cfg.depthWrite)
				.SetDepthFunc(cfg.depthFunc)
				.SetStencilEnable(false);

			// Premultiplied: programs.forward.Transparent returns radiance already weighted by its own
			// coverage, so the reflection reaches the film undimmed by the material's alpha while
			// the transmitted lobe is thinned in the shader. kSrcAlpha here would scale both.
			auto blend = bgpu::BlendState{};
			// Composited colour has no single depth; zero alpha excludes it from TAA depth validation.
			if (cfg.blend)
			{
				blend.SetRenderTarget(
					0,
					bgpu::BlendState::RenderTarget{}
						.EnableBlend()
						.SetSrcBlend(bgpu::BlendFactor::kOne)
						.SetDestBlend(bgpu::BlendFactor::kInvSrcAlpha)
						.SetBlendOp(bgpu::BlendOp::kAdd)
						.SetSrcBlendAlpha(bgpu::BlendFactor::kZero)
						.SetDestBlendAlpha(bgpu::BlendFactor::kZero)
						.SetBlendOpAlpha(bgpu::BlendOp::kAdd));
			}

			pipelineDesc.renderState = bgpu::RenderState()
			                               .SetRasterState(raster)
			                               .SetBlendState(blend)
			                               .SetDepthStencilState(depth);

			return pipelineDesc;
		}
	}

	bool
	DrawBucketDissolves(const DrawBucketDesc& desc) noexcept
	{
		return desc.geom != GeometryStage::kGrass;
	}

	ForwardPhases::ForwardPhases(const PassInitContext& ctx)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		core::ensure(ctx.drawBucketTable != nullptr, "The pass keys its kernels by draw bucket");
		m_DrawBucketTable = ctx.drawBucketTable;
	}

	void
	ForwardPhases::AddDrawBucketKernels(const PassInitContext& ctx, const DrawBucketMask& demanded)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		const uint32_t count = m_DrawBucketTable->Count();
		if (m_Kernels.size() < count)
		{
			m_Kernels.resize(count);
			m_DissolveKernels.resize(count);
		}

		for (uint32_t bucket = 0; bucket < count; ++bucket)
		{
			if (demanded.test(bucket) && !m_Kernels[bucket].pipeline.IsInitialized())
			{
				core::ensure(
					!m_DrawBucketTable->Transparent(bucket),
					"A transparent bucket demands the shared kernel, never one of its own");
				const DrawBucketDesc& desc = m_DrawBucketTable->Desc(bucket);
				const auto            slot = GameSlot(desc.material);
				const bool toon = slot.has_value() && *slot < m_ToonCharacterSlots.size() &&
				                  m_ToonCharacterSlots[*slot];
				ctx.pipelines->Add(
					m_Kernels[bucket],
					ForwardPipelineDesc(ctx.device, ConfigFor(desc, DrawLane::kAtRest, toon)));
				if (DrawBucketDissolves(desc))
				{
					ctx.pipelines->Add(
						m_DissolveKernels[bucket],
						ForwardPipelineDesc(
							ctx.device,
							ConfigFor(desc, DrawLane::kDissolve, toon)));
				}
			}
		}
	}

	void
	ForwardPhases::SetToonCharacterSlots(std::vector<bool> slots)
	{
		m_ToonCharacterSlots = std::move(slots);
	}

	void
	ForwardPhases::AddTransparentKernel(const PassInitContext& ctx)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		if (!m_TransparentKernel.pipeline.IsInitialized())
		{
			ctx.pipelines->Add(
				m_TransparentKernel,
				ForwardPipelineDesc(
					ctx.device,
					PsoConfig{ std::string(c_TransparentSrc),
			                   bgpu::RasterCullMode::kNone,
			                   false,
			                   true,
			                   bgpu::ComparisonFunc::kLess,
			                   c_AnyGeomSrc }));
		}
	}

	void
	ForwardPhases::CheckBindings() const
	{
		CheckKernelNames(m_Kernels);
		CheckKernelNames(m_DissolveKernels);
		CheckKernelNames({ &m_TransparentKernel, 1 });
	}

	void
	ForwardPhases::CheckKernelNames(std::span<const bgpu::MeshletKernel> kernels) const
	{
		// The buckets are demand-built, so nothing reads their names off until a first one is;
		// EnsureDrawBucketPipelinesExist re-checks after every build.
		if (!bgpu::AnyInitialized(kernels))
		{
			return;
		}

		auto check = BindingNameCheck("ForwardPhases"sv, kernels);
		check.Check("forwardData"sv, GetUniformKeys(c_ForwardDataBuffers))
			.Check("expansionData"sv, GetUniformKeys(c_ExpansionBuffers))
			.Check("expansionData"sv, c_ExpansionDataFields)
			.Check("viewData"sv, c_ViewDataFields)
			.Check("materialData"sv, GetUniformKeys(c_MaterialBuffers))
			.Check("materialData"sv, c_MaterialDataFields)
			.Check("skinnedData"sv, GetUniformKeys(c_SkinnedBuffers));
		GrassForwardPhase::CheckBindings(check);
	}

	const IForwardPhase&
	ForwardPhases::Phase(const ForwardPhase phase) const noexcept
	{
		switch (phase)
		{
		case ForwardPhase::kWorld:
			return m_World;
		case ForwardPhase::kGrass:
			return m_Grass;
		case ForwardPhase::kSkinned:
			return m_Skinned;
		case ForwardPhase::kTransparent:
			return m_Transparent;
		}
		core::fatal("An unknown forward phase");
	}

	void
	ForwardPhases::AttachToFrameGraph(
		FrameGraph&        fg,
		const DrawData&    draw,
		const ForwardPhase which)
	{
		const IForwardPhase& phase = Phase(which);
		if (!phase.HasWork(draw))
		{
			return;
		}

		auto desc = PassDesc();
		desc.SetName("Forward {} {}", phase.Name(), draw.drawIdx)
			.AddRenderTarget(c_BackbufferName)
			.AddDepthWrite(c_DepthName);

		for (const auto& binding : c_ForwardDataBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		for (const auto& binding : c_ExpansionBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		DeclareMeshletCullBuffers(desc);

		for (const auto& binding : c_MaterialBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}
		desc.AddBufferRead(c_ToonShadingRigBlocksName, bgpu::BarrierSyncFlag::kPixelShader);

		phase.Declare(desc);

		desc.SetExec([this, draw, &phase](const PassContext& resources) {
			Execute(phase, draw, resources);
		});

		fg.AddPass(std::move(desc));
	}

	bgpu::MeshletKernel*
	ForwardPhases::BindDrawBucketKernel(
		const uint32_t      bucket,
		const DrawLane      lane,
		bgpu::MeshletState& state,
		const DrawData&     draw,
		const PassContext&  resources)
	{
		auto& kernels = lane == DrawLane::kDissolve ? m_DissolveKernels : m_Kernels;
		if (bucket >= kernels.size() || !kernels[bucket].pipeline.IsInitialized())
		{
			return nullptr;
		}

		bgpu::MeshletKernel& kernel = kernels[bucket];
		BindKernel(kernel, draw, resources);
		state.kernel      = &kernel;
		state.frameBuffer = bgpu::FrameBuffer()
		                        .AddColorAttachment(draw.targets.sceneColor)
		                        .AddColorAttachment(draw.targets.motionVector)
		                        .SetDepthAttachment(draw.targets.depth);
		return &kernel;
	}

	bgpu::MeshletKernel*
	ForwardPhases::BindTransparentKernel(
		bgpu::MeshletState& state,
		const DrawData&     draw,
		const PassContext&  resources)
	{
		if (!TransparentInitialized())
		{
			return nullptr;
		}

		BindKernel(m_TransparentKernel, draw, resources);
		state.kernel      = &m_TransparentKernel;
		state.frameBuffer = bgpu::FrameBuffer()
		                        .AddColorAttachment(draw.targets.sceneColor)
		                        .SetDepthAttachment(draw.targets.depth);
		return &m_TransparentKernel;
	}

	void
	ForwardPhases::BindKernel(
		bgpu::MeshletKernel& kernel,
		const DrawData&      draw,
		const PassContext&   resources)
	{
		if (auto foundForwardData = kernel.FindUniforms("forwardData"))
		{
			BindSceneBuffers(*foundForwardData, c_ForwardDataBuffers, resources);
		}

		if (auto foundExpansion = kernel.FindUniforms("expansionData"))
		{
			BindSceneBuffers(*foundExpansion, c_ExpansionBuffers, resources);
			BindMeshletCullBuffers(*foundExpansion, resources);
		}

		if (auto foundSkinnedData = kernel.FindUniforms("skinnedData"))
		{
			BindSceneBuffers(*foundSkinnedData, c_SkinnedBuffers, resources);
		}

		if (auto foundViewData = kernel.FindUniforms("viewData"))
		{
			auto& viewData           = *foundViewData;
			viewData["viewProj"]     = draw.viewState.viewProj;
			viewData["prevViewProj"] = draw.viewState.prevViewProj;
			viewData["jitter"]       = draw.viewState.jitter;
			viewData["prevJitter"]   = draw.viewState.prevJitter;
			viewData["time"]         = draw.clock.time;
			viewData["prevTime"]     = draw.clock.prevTime;
		}

		if (auto foundMatData = kernel.FindUniforms("materialData"))
		{
			auto& matData = *foundMatData;
			// Bound from the draw rather than the graph: the pair is one allocation and two
			// descriptors, and the graph tracks resource state -- a view is not a resource. It is
			// still declared to the graph (c_MaterialBuffers) so the arena's barriers are placed.
			matData["materials"] = draw.materialArena;

			matData["anisoLinearWrapSampler"].SetIfValid(draw.samplers.anisoLinearWrap);
			matData["linearClampSampler"].SetIfValid(draw.samplers.linearClamp);

			matData["irradianceMap"].SetIfValid(draw.lighting.env.irradiance);
			matData["prefilterMap"].SetIfValid(draw.lighting.env.prefilter);
			matData["brdfLUT"].SetIfValid(draw.lighting.env.brdfLut);
			matData["cameraPos"].SetIfValid(draw.viewState.cameraPos);
			matData["exposure"].SetIfValid(draw.lighting.exposure);
			matData["envRotation"].SetIfValid(draw.lighting.envRotation);
			matData["alphaHashSeed"].SetIfValid(draw.viewState.alphaHashSeed);
			matData["sunDirection"].SetIfValid(draw.lighting.sunDirection);
			matData["sunRadiance"].SetIfValid(draw.lighting.sunRadiance);
			matData["toonShadingRigBlocks"].SetIfValid(
				resources.GetBuffer(c_ToonShadingRigBlocksName));
		}
	}

	void
	ForwardPhases::Execute(
		const IForwardPhase& phase,
		const DrawData&      draw,
		const PassContext&   resources)
	{
		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		auto state = bgpu::MeshletState();
		state.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);

		phase.Record(*this, state, draw, resources);
	}
}
