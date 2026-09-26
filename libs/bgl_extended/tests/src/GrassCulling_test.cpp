#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "constants/constants.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/GraphicsBase.h"
#include "gfx/RenderContext.h"
#include "gfx/RenderTargetBase.h"
#include "passes/CompactInstancesPass.h"
#include "passes/DrawData.h"
#include "passes/ForwardPhases.h"
#include "passes/PassInitContext.h"
#include "pipeline/PipelineBatch.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "scene/scene_buffer_names.h"
#include "types/Barrier.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <array>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/Grass.h>
#include <bgl/Camera.h>
#include <bgl/GrassHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/Viewport.h>
#include <bgl/types/GrassDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl_common/Frustum.h>
#include <bgl_common/idl/CullStats.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// What the grass stage's amplification groups decided, read off cull.stats: every chunk of a field
// in view is tested and none culled, a field behind the camera is culled whole, and one past the fade
// emits nothing although it is in view. The counters are written only under BERNINI_GPU_DEBUG.

#if defined(BERNINI_GPU_DEBUG)

namespace
{
	constexpr uint32_t c_W = 320;
	constexpr uint32_t c_H = 200;

	constexpr uint32_t c_Side        = 32;  // 1024 clumps, 16 chunks
	constexpr float    c_Spacing     = 0.2f;
	constexpr uint32_t c_BladesPer   = 4;
	constexpr float    c_FadeStart   = 5.0f;
	constexpr float    c_FadeEnd     = 30.0f;
	constexpr uint32_t c_ChunkCount  = c_Side * c_Side / assetlib::c_GrassClumpsPerChunk;
	constexpr uint32_t c_TotalBlades = c_Side * c_Side * c_BladesPer;

	/** A flat field over XZ at y = 0, growing up, chunked in runs of whole rows. */
	assetlib::BGrassFields
	MakeField()
	{
		auto grass  = assetlib::BGrassFields();
		grass.looks = { "unused.bgrass" };
		grass.names = { "Ground" };

		const float origin = -0.5f * c_Spacing * static_cast<float>(c_Side - 1);
		for (uint32_t z = 0; z < c_Side; ++z)
			for (uint32_t x = 0; x < c_Side; ++x)
				grass.clumps.push_back(
					assetlib::GrassClump{ .position = glm::vec3(
											  origin + c_Spacing * static_cast<float>(x),
											  0.0f,
											  origin + c_Spacing * static_cast<float>(z)),
				                          .heightScale = 1.0f,
				                          .normal      = glm::vec3(0.0f, 1.0f, 0.0f),
				                          .color       = glm::u8vec4(255) });

		auto field = assetlib::GrassField{ .mesh = 0, .look = 0, .firstChunk = 0, .chunkCount = 0 };
		for (uint32_t first = 0; first < grass.clumps.size();
		     first += assetlib::c_GrassClumpsPerChunk)
		{
			glm::vec3 lo(1e30f);
			glm::vec3 hi(-1e30f);
			for (uint32_t k = first; k < first + assetlib::c_GrassClumpsPerChunk; ++k)
			{
				lo = glm::min(lo, grass.clumps[k].position);
				hi = glm::max(hi, grass.clumps[k].position);
			}
			grass.chunks.push_back(
				assetlib::GrassChunk{ .boundingCenter = (lo + hi) * 0.5f,
			                          .boundingRadius = glm::distance(lo, hi) * 0.5f,
			                          .firstClump     = first,
			                          .clumpCount     = assetlib::c_GrassClumpsPerChunk,
			                          .maxHeightScale = 1.0f });
			++field.chunkCount;
		}
		grass.fields = { field };
		return grass;
	}

	struct Harness
	{
		bgl::GraphicsRef                       gfx;
		bgl::GraphicsBase*                     gfxBase = nullptr;
		core::SharedRef<bgl::IResourceManager> resourceManager;
		bgl::IDevice*                          device = nullptr;
		bgl::RenderTargetRef                   target;
		bgl::RenderTargetBase*                 targetBase = nullptr;
		bgl::SceneRef                          sceneRef;
		bgl::SceneViewRef                      viewRef;
		bgl::Scene*                            scene = nullptr;
		bgl::SceneView*                        view  = nullptr;
		bgl::CompactInstancesPass              compactPass;
		bgl::ForwardPhases                     forwardPhases;

		Harness()
		{
			auto opts                     = bgl::GraphicsOptions();
			opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
			opts.enableDebugLayer         = true;
			opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
			gfx                           = bgl::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			gfxBase         = gfx->As<bgl::GraphicsBase>();
			resourceManager = gfxBase->GetResourceManagerCpy();
			device          = gfxBase->GetDevice();

			auto td     = bgl::RenderTargetDesc();
			td.width    = static_cast<int>(c_W);
			td.height   = static_cast<int>(c_H);
			td.headless = true;
			target      = gfx->CreateRenderTarget(td);
			targetBase  = target->As<bgl::RenderTargetBase>();

			auto sceneDesc                        = bgl::SceneDesc();
			sceneDesc.initialGeom                 = 4;
			sceneDesc.initialMeshlets             = 16;
			sceneDesc.initialSubmeshes            = 4;
			sceneDesc.initialVertexBufferByteSize = 4096;
			sceneDesc.initialIndices              = 64;
			sceneDesc.initialPbrMaterials         = 4;
			sceneRef                              = gfx->CreateScene(sceneDesc);
			viewRef                               = gfx->CreateSceneView(sceneRef, 4);
			scene                                 = sceneRef->As<bgl::Scene>();
			view                                  = viewRef->As<bgl::SceneView>();
			bgl::test::ApplyEnvironment(sceneRef.Get(), viewRef.Get());

			const auto material = sceneRef->CreatePbrMaterial(bgl::PbrMaterialDesc());
			const auto ground   = sceneRef->AddPlaneGeom(1, 1, 1.0f, 1.0f, material);
			REQUIRE(viewRef->CreateStaticMeshInstance(ground, glm::mat4(1.0f)).IsValid());

			auto look                                   = bgl::GrassDesc();
			look.material                               = material;
			look.clump.bladesPerClump                   = c_BladesPer;
			look.density.fadeStart                      = c_FadeStart;
			look.density.fadeEnd                        = c_FadeEnd;
			const std::array<bgl::GrassHandle, 1> looks = { sceneRef->CreateGrass(look) };
			sceneRef->AttachGrass(ground, MakeField(), 0, looks);

			view->RefreshGrass();
			const bgl::DrawBucketTable& table     = gfxBase->GetRenderContext()->DrawBuckets();
			auto                        pipelines = bgl::PipelineBatch(device);
			const auto ctx = bgl::PassInitContext{ device, &pipelines, resourceManager, &table };
			compactPass.Init(ctx);
			forwardPhases.Init(ctx);
			forwardPhases.AddDrawBucketKernels(ctx, view->GrassDrawBuckets());
			pipelines.Build();
			forwardPhases.CheckBindings();
		}

		Harness(const Harness&) = delete;
		Harness(Harness&&)      = delete;
		Harness&
		operator=(const Harness&) = delete;
		Harness&
		operator=(Harness&&) = delete;

		~Harness()
		{
			compactPass.Release(false);
			forwardPhases.Release();
		}

		/** One frame from `eye` looking at `at`, and the counters it left. */
		[[nodiscard]] bgl::idl::CullStats
		Frame(const glm::vec3& eye, const glm::vec3& at)
		{
			auto camera = bgl::Camera();
			camera.LookAt(eye, at, glm::vec3(0.0f, 1.0f, 0.0f))
				.Perspective(glm::radians(60.0f), static_cast<float>(c_W) / c_H, 0.1f, 500.0f);
			const glm::mat4 viewProj = camera.GetViewProjection();

			auto rbDesc      = bgl::ReadbackBufferDesc();
			rbDesc.byteSize  = sizeof(bgl::idl::CullStats);
			rbDesc.debugName = "Grass Cull Stats Readback";
			auto readback    = resourceManager->CreateReadbackBuffer(rbDesc);

			auto listDesc  = bgl::CommandListDesc();
			listDesc.type  = bgl::QueueType::kGraphics;
			auto allocator = device->CreateCommandAllocator();
			auto cmdList   = device->CreateCommandList(listDesc, allocator, resourceManager);
			auto cmdQueue  = device->CreateCommandQueue(bgl::QueueType::kGraphics);

			bgl::FrameGraph fg;
			fg.RegisterQueue("main", cmdQueue, cmdList);
			fg.ImportTexture(bgl::c_BackbufferName, targetBase->GetSceneColorTexture());
			fg.ImportTexture(bgl::c_MotionVectorsName, targetBase->GetMotionVectorTexture());
			fg.ImportTexture(bgl::c_DepthName, targetBase->GetDepthTexture());

			fg.SetResourceNamespace(view->GetResourceNamespace());
			scene->AttachToFrameGraph(fg, 0);
			view->AttachToFrameGraph(fg, 0);

			auto draw      = bgl::DrawData();
			draw.view      = viewRef;
			draw.cullState = &view->GetCullState(0);
			draw.viewState.viewport =
				bgl::Viewport(static_cast<float>(c_W), static_cast<float>(c_H));
			draw.viewState.viewProj     = viewProj;
			draw.viewState.prevViewProj = viewProj;
			draw.viewState.cullView     = bgl::BuildCullView(viewProj);
			draw.viewState.cameraPos    = eye;
			draw.targets.sceneColor     = targetBase->GetSceneColorRtv();
			draw.targets.motionVector   = targetBase->GetMotionVectorRtv();
			draw.targets.depth          = targetBase->GetDepthDsv();
			draw.materialArena          = scene->GetMaterialBinding();
			draw.samplers.anisoLinearWrap =
				scene->GetSampler(bgl::Scene::StandardSampler::kAnisoLinearWrap);
			draw.samplers.linearClamp =
				scene->GetSampler(bgl::Scene::StandardSampler::kLinearClamp);
			draw.lighting.env = view->GetEnvironmentMap();

			fg.SetResourceNamespace(view->GetCullNamespace(0));
			compactPass.AttachToFrameGraph(fg, draw);
			forwardPhases.AttachToFrameGraph(fg, draw, bgl::ForwardPhase::kGrass);

			fg.AddPass(
				bgl::PassDesc()
					.SetName("Grass Cull Stats Readback")
					.AddBufferArg(
						bgl::c_CullStatsName,
						bgl::BarrierSyncFlag::kCopy,
						bgl::BarrierAccessFlag::kCopySource)
					.SetSideEffect()
					.SetExec([&](const bgl::PassContext& ctx) {
						ctx.GetCommandList()->CopyBufferToReadback(
							readback,
							ctx.GetBuffer(bgl::c_CullStatsName));
					}));

			fg.Compile(resourceManager.Get());
			cmdList->Open(cmdQueue, allocator);
			fg.Execute();
			cmdList->Close();
			cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

			const auto* mapped =
				static_cast<const bgl::idl::CullStats*>(resourceManager->MapReadback(readback));
			REQUIRE(mapped != nullptr);
			bgl::idl::CullStats stats;
			stats.grassChunksTested  = mapped->grassChunksTested;
			stats.grassChunksCulled  = mapped->grassChunksCulled;
			stats.grassBladesEmitted = mapped->grassBladesEmitted;
			resourceManager->UnmapReadback(readback);
			return stats;
		}
	};
}

TEST_CASE(
	"The grass stage culls chunks to the frustum and thins blades by distance",
	"[grass][cull]")
{
	Harness harness;

	const bgl::idl::CullStats near = harness.Frame(glm::vec3(0.0f, 2.0f, 4.0f), glm::vec3(0.0f));
	INFO(
		"near: tested " << near.grassChunksTested << ", culled " << near.grassChunksCulled
						<< ", blades " << near.grassBladesEmitted);
	CHECK(near.grassChunksTested == c_ChunkCount);
	CHECK(near.grassChunksCulled == 0u);
	CHECK(near.grassBladesEmitted > 0u);
	CHECK(near.grassBladesEmitted <= c_TotalBlades);

	// Far enough behind that every chunk's sphere -- two rows of the field, inflated by the blades'
	// reach -- is wholly behind the camera, not merely its centre.
	const bgl::idl::CullStats away =
		harness.Frame(glm::vec3(0.0f, 2.0f, 15.0f), glm::vec3(0.0f, 2.0f, 40.0f));
	CHECK(away.grassChunksTested == c_ChunkCount);
	CHECK(away.grassChunksCulled == c_ChunkCount);
	CHECK(away.grassBladesEmitted == 0u);

	const bgl::idl::CullStats mid = harness.Frame(glm::vec3(0.0f, 4.0f, 18.0f), glm::vec3(0.0f));
	INFO("mid blades " << mid.grassBladesEmitted);
	CHECK(mid.grassChunksCulled == 0u);
	CHECK(mid.grassBladesEmitted > 0u);
	CHECK(mid.grassBladesEmitted < near.grassBladesEmitted);

	const bgl::idl::CullStats far = harness.Frame(glm::vec3(0.0f, 10.0f, 60.0f), glm::vec3(0.0f));
	CHECK(far.grassChunksCulled == 0u);
	CHECK(far.grassBladesEmitted == 0u);
}

#endif
