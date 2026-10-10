#include "passes/ForwardPhases.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/frame_constants.h"
#include "passes/BindingNameCheck.h"
#include "passes/DrawData.h"
#include "passes/SceneBindings.h"
#include "passes/draw_bucket_config.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "scene/ground_color.h"
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
		constexpr std::array<std::string_view, 11> c_MaterialDataFields = {
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
		};

		// Only the toon character's programs declare it; see lib.forward.ToonData.
		constexpr std::array<std::string_view, 1> c_ToonDataFields = {
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
			bgpu::ComparisonFunc depthFunc = bgpu::ComparisonFunc::kGreater;
			std::string_view     geomSrc;
			// The dissolve lane's entries: MSDissolve and PSDissolve, which carry and read the
			// placement's dissolve code, beside the at-rest lane's MSMain and PSMain.
			std::string_view meshEntry  = "MSMain"sv;
			std::string_view pixelEntry = "PSMain"sv;
			// A blade's programs write a third target, its height above its root, after the
			// velocity: GrassForwardPhase attaches it.
			bool grassRootHeight = false;
			// Water's: no depth attached, the test done in the pixel stage against the depth read
			// there, and colour blended premultiplied with alpha left as the ground wrote it.
			bool water = false;
		};

		// Every bucket kernel is opaque-shaped; only the shared blend kernel differs.
		// A toon character's bucket at rest draws through MSToon, whose vertices carry the placement's
		// toon shading rig block; its dissolve lane, and every other bucket, through the shared ones.
		// Only a mesh tier has MSToon: a blade and a terrain patch carry no placement's rig.
		PsoConfig
		ConfigFor(
			const DrawBucketDesc& desc,
			const DrawLane        lane,
			const bool            toonCharacter,
			const bool            water)
		{
			auto config =
				PsoConfig{ DrawBucketPixelSrc(desc),       DrawBucketCullMode(desc),   true, false,
				           bgpu::ComparisonFunc::kGreater, DrawBucketGeometrySrc(desc) };
			if (lane == DrawLane::kDissolve)
			{
				config.meshEntry  = "MSDissolve"sv;
				config.pixelEntry = "PSDissolve"sv;
			}
			else if (
				toonCharacter && desc.geom != GeometryStage::kGrass &&
				desc.geom != GeometryStage::kTerrain)
			{
				config.meshEntry = "MSToon"sv;
			}
			config.grassRootHeight = desc.geom == GeometryStage::kGrass;
			config.water           = water;
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
			if (cfg.grassRootHeight)
			{
				pipelineDesc.AddRtvFormat(c_GrassRootHeightFormat);
			}
			if (!cfg.water)
			{
				pipelineDesc.SetDsvFormat(bgpu::Format::D32);
			}

			auto raster = bgpu::RasterState();
			raster.SetFillMode(bgpu::RasterFillMode::kSolid)
				.SetCullMode(cfg.cull)
				.SetFrontCounterClockwise(true)
				.SetDepthClipEnable(true);

			auto depth = bgpu::DepthStencilState{};
			depth.SetDepthTestEnable(!cfg.water)
				.SetDepthWriteEnable(cfg.depthWrite && !cfg.water)
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

			// The scene alpha under water is the TAA marker its ground wrote, and the depth it
			// vouches for is still the ground's: masked out rather than overwritten. The velocity
			// target is the water surface's own, unblended.
			if (cfg.water)
			{
				constexpr auto c_Rgb = static_cast<bgpu::ColorMask>(
					std::to_underlying(bgpu::ColorMask::kRed) |
					std::to_underlying(bgpu::ColorMask::kGreen) |
					std::to_underlying(bgpu::ColorMask::kBlue));
				blend.SetRenderTarget(
					0,
					bgpu::BlendState::RenderTarget{}
						.EnableBlend()
						.SetSrcBlend(bgpu::BlendFactor::kOne)
						.SetDestBlend(bgpu::BlendFactor::kInvSrcAlpha)
						.SetBlendOp(bgpu::BlendOp::kAdd)
						.SetColorWriteMask(c_Rgb));
			}

			pipelineDesc.renderState = bgpu::RenderState()
			                               .SetRasterState(raster)
			                               .SetBlendState(blend)
			                               .SetDepthStencilState(depth);

			return pipelineDesc;
		}
	}

	namespace
	{
		// The terrain's own stages into two targets, the albedo and the ground cover, with no depth
		// -- a heightfield seen from straight above covers each texel once -- and no velocity, which
		// nothing reprojects.
		bgpu::MeshletPipelineDesc
		GroundColorPipelineDesc(bgpu::IDevice* device, const DrawBucketDesc& desc)
		{
			const std::string geometry(DrawBucketGeometrySrc(desc));

			auto pipelineDesc       = bgpu::MeshletPipelineDesc();
			pipelineDesc.ampShader  = device->CreateShader(geometry, "ASMain");
			pipelineDesc.meshShader = device->CreateShader(geometry, "MSMain");
			pipelineDesc.pixelShader =
				device->CreateShader(DrawBucketGroundColorSrc(desc), "PSMain");
			pipelineDesc.AddRtvFormat(c_GroundColorFormat);
			pipelineDesc.AddRtvFormat(c_GroundCoverFormat);

			auto raster = bgpu::RasterState();
			raster.SetFillMode(bgpu::RasterFillMode::kSolid)
				.SetCullMode(bgpu::RasterCullMode::kNone)
				.SetFrontCounterClockwise(true)
				.SetDepthClipEnable(false);

			auto depth = bgpu::DepthStencilState{};
			depth.SetDepthTestEnable(false).SetDepthWriteEnable(false).SetStencilEnable(false);

			pipelineDesc.renderState = bgpu::RenderState()
			                               .SetRasterState(raster)
			                               .SetBlendState(bgpu::BlendState{})
			                               .SetDepthStencilState(depth);
			return pipelineDesc;
		}
	}

	bool
	DrawBucketDissolves(const DrawBucketDesc& desc) noexcept
	{
		return desc.geom != GeometryStage::kGrass && desc.geom != GeometryStage::kTerrain;
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
			m_GroundColorKernels.resize(count);
		}

		for (uint32_t bucket = 0; bucket < count; ++bucket)
		{
			if (demanded.test(bucket) && !m_Kernels[bucket].pipeline.IsInitialized())
			{
				core::ensure(
					!m_DrawBucketTable->Transparent(bucket),
					"A transparent bucket demands the shared kernel, never one of its own");
				const DrawBucketDesc&               desc    = m_DrawBucketTable->Desc(bucket);
				const std::optional<SurfaceShading> shading = ShadingOf(desc.material);
				const bool toon  = shading == SurfaceShading::kToonCharacter;
				const bool water = m_DrawBucketTable->Water(bucket);
				core::ensure(
					!water || desc.geom == GeometryStage::kStaticMesh,
					"Water draws on the static tier alone; every other door refuses it");
				ctx.pipelines->Add(
					m_Kernels[bucket],
					ForwardPipelineDesc(
						ctx.device,
						ConfigFor(desc, DrawLane::kAtRest, toon, water)));
				if (DrawBucketDissolves(desc))
				{
					ctx.pipelines->Add(
						m_DissolveKernels[bucket],
						ForwardPipelineDesc(
							ctx.device,
							ConfigFor(desc, DrawLane::kDissolve, toon, water)));
				}
				// Built with the bucket whether or not a look ever asks for the ground's colour: one
				// more pipeline per terrain material kind, against demand-building it a frame late.
				if (desc.geom == GeometryStage::kTerrain)
				{
					ctx.pipelines->Add(
						m_GroundColorKernels[bucket],
						GroundColorPipelineDesc(ctx.device, desc));
				}
			}
		}
	}

	void
	ForwardPhases::SetSurfaceShading(std::vector<SurfaceShading> slots)
	{
		m_SurfaceShading = std::move(slots);
	}

	std::optional<SurfaceShading>
	ForwardPhases::ShadingOf(const MaterialType material) const noexcept
	{
		const std::optional<uint32_t> slot = GameSlot(material);
		if (!slot.has_value() || *slot >= m_SurfaceShading.size())
		{
			return std::nullopt;
		}
		return m_SurfaceShading[*slot];
	}

	bool
	ForwardPhases::IsWaterBucket(const uint32_t bucket) const noexcept
	{
		return bucket < m_DrawBucketTable->Count() && m_DrawBucketTable->Water(bucket);
	}

	bool
	ForwardPhases::DemandsWater(const DrawData& draw) const
	{
		const auto* view = draw.view->As<SceneView>();
		core::ensure(view != nullptr, "The water phase requires a bgl::SceneView");

		const DrawBucketMask& demanded = view->DemandedDrawBuckets();
		for (uint32_t bucket = 0, count = m_DrawBucketTable->Count(); bucket < count; ++bucket)
		{
			if (demanded.test(bucket) && IsWaterBucket(bucket))
			{
				return true;
			}
		}
		return false;
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
			                   bgpu::ComparisonFunc::kGreater,
			                   c_AnyGeomSrc }));
		}
	}

	void
	ForwardPhases::CheckBindings() const
	{
		CheckKernelNames(m_Kernels);
		CheckKernelNames(m_DissolveKernels);
		CheckKernelNames(m_GroundColorKernels);
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
			.Check("toonData"sv, c_ToonDataFields)
			.Check("skinnedData"sv, GetUniformKeys(c_SkinnedBuffers))
			.Check("impostorData"sv, GetUniformKeys(c_ImpostorBuffers));
		GrassForwardPhase::CheckBindings(check);
		TerrainForwardPhase::CheckBindings(check);
		WaterForwardPhase::CheckBindings(check);
	}

	const IForwardPhase&
	ForwardPhases::Phase(const ForwardPhase phase) const noexcept
	{
		switch (phase)
		{
		case ForwardPhase::kTerrain:
			return m_Terrain;
		case ForwardPhase::kWorld:
			return m_World;
		case ForwardPhase::kImpostor:
			return m_Impostor;
		case ForwardPhase::kGrass:
			return m_Grass;
		case ForwardPhase::kSkinned:
			return m_Skinned;
		case ForwardPhase::kWater:
			return m_Water;
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
		desc.SetName("Forward {} {}", phase.Name(), draw.drawIdx).AddRenderTarget(c_BackbufferName);
		if (phase.WritesDepth())
		{
			desc.AddDepthWrite(c_DepthName);
		}

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

		phase.Declare(desc, draw);

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
		                        .AddColorAttachment(draw.targets.motionVector);
		if (!IsWaterBucket(bucket))
		{
			state.frameBuffer.SetDepthAttachment(draw.targets.depth);
		}
		return &kernel;
	}

	void
	ForwardPhases::AttachGroundColor(
		FrameGraph&             fg,
		const DrawData&         draw,
		bgpu::IResourceManager* resourceManager)
	{
		const auto* view = draw.view->As<SceneView>();
		core::ensure(view != nullptr, "Ground Color requires a bgl::SceneView");
		const SceneView::GroundColorTarget& target = view->GetGroundColor();
		if (target.rect.size <= 0.0f || target.texture.IsNull() || !m_Terrain.HasWork(draw))
		{
			return;
		}

		auto desc = PassDesc();
		desc.SetName("Ground Color {}", draw.drawIdx)
			.AddRenderTarget(c_GroundColorName)
			.AddRenderTarget(c_GroundCoverName);
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
		m_Terrain.DeclareBuffers(desc);

		// Cleared here rather than by the frame's Clear: the textures are the view's, and a texel no
		// terrain covers must read as no ground colour and as ground fully covered.
		desc.SetExec([this, draw, resourceManager, rtv = target.rtv, coverRtv = target.coverRtv](
						 const PassContext& resources) {
			float noGround[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			float covered[4]  = { 1.0f, 1.0f, 1.0f, 1.0f };
			resourceManager->ClearRtv(resources.GetCommandList(), rtv, noGround);
			resourceManager->ClearRtv(resources.GetCommandList(), coverRtv, covered);

			auto state = bgpu::MeshletState();
			state.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
			m_Terrain.RecordGroundColor(*this, state, draw, resources);
		});

		fg.AddPass(std::move(desc));
	}

	bgpu::MeshletKernel*
	ForwardPhases::BindGroundColorKernel(
		const uint32_t      bucket,
		bgpu::MeshletState& state,
		const DrawData&     draw,
		const PassContext&  resources)
	{
		if (bucket >= m_GroundColorKernels.size() ||
		    !m_GroundColorKernels[bucket].pipeline.IsInitialized())
		{
			return nullptr;
		}

		const auto* view = draw.view->As<SceneView>();
		core::ensure(view != nullptr, "Ground Color requires a bgl::SceneView");

		bgpu::MeshletKernel& kernel = m_GroundColorKernels[bucket];
		BindKernel(kernel, draw, resources);
		state.kernel      = &kernel;
		state.frameBuffer = bgpu::FrameBuffer()
		                        .AddColorAttachment(view->GetGroundColor().rtv)
		                        .AddColorAttachment(view->GetGroundColor().coverRtv);
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
		}

		WaterForwardPhase::Bind(kernel, draw, resources);

		if (auto foundImpostorData = kernel.FindUniforms("impostorData"))
		{
			(*foundImpostorData)["impostors"] = draw.impostorArena;
		}

		if (auto foundToonData = kernel.FindUniforms("toonData"))
		{
			auto& toonData = *foundToonData;
			toonData["toonShadingRigBlocks"].SetIfValid(
				resources.GetBuffer(c_ToonShadingRigBlocksName));
		}
	}

	void
	ForwardPhases::Execute(
		const IForwardPhase& phase,
		const DrawData&      draw,
		const PassContext&   resources)
	{
		auto state = bgpu::MeshletState();
		state.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);

		phase.Record(*this, state, draw, resources);
	}
}
