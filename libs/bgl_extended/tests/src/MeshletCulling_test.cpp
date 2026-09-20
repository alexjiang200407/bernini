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
#include "passes/PassInitContext.h"
#include "passes/StaticDepthPass.h"
#include "pipeline/PipelineBatch.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "scene/scene_buffer_names.h"
#include "types/Barrier.h"
#include "types/QueueType.h"
#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <bgl/types/BlobShadowDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl_common/Frustum.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/CullStats.h>
#include <bgl_common/idl/Meshlet.h>
#include <bgl_common/idl/Submesh.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <string>
#include <vector>

// Meshlet culling may only ever drop what the raster would have clipped whole. The first case pins
// that against the same floor drawn with nothing culled -- too many meshlets to compact, each sphere
// inflated past the whole scene, so the same shaders dispatch and keep every one -- through both
// culling paths: the amplification group's compaction, and the mesh stage's own test for a submesh
// too large to compact. The second pins that culling happens at all, which an image cannot show.

namespace
{
	constexpr uint32_t c_W = 256;
	constexpr uint32_t c_H = 160;

	// Wider than the frustum on every side and deeper than its far plane, and behind the camera too,
	// so every plane of the frustum cuts it.
	constexpr uint32_t c_FloorSegments = 96;
	constexpr float    c_FloorSize     = 400.0f;

	struct FloorVertex
	{
		glm::vec3 position;
		glm::vec3 normal;
	};

	constexpr uint16_t c_Stride = sizeof(FloorVertex);

	// A row of up to this many quads is one meshlet: its two rows of vertices fill the 64 a meshlet
	// holds.
	constexpr uint32_t c_QuadsPerMeshlet = 31;

	/**
	 * Appends a horizontal grid facing +Y, centred on `centre`, to `mesh`'s one submesh, cut into
	 * meshlets a row strip at a time with bounds measured the way the cook's are -- enclosing every
	 * vertex -- and appended after the meshlets already there.
	 */
	void
	AppendFloor(
		assetlib::BMesh& mesh,
		glm::vec3        centre,
		float            size,
		uint32_t         xSegments,
		uint32_t         zSegments)
	{
		assetlib::Submesh& submesh = mesh.submeshes.front();

		const uint32_t base      = submesh.vertexCount;
		const uint32_t rowStride = xSegments + 1u;

		auto vertexAt = [&](uint32_t x, uint32_t z) {
			const float u = static_cast<float>(x) / static_cast<float>(xSegments);
			const float v = static_cast<float>(z) / static_cast<float>(zSegments);
			return centre + glm::vec3((u - 0.5f) * size, 0.0f, (0.5f - v) * size);
		};

		for (uint32_t z = 0u; z <= zSegments; ++z)
		{
			for (uint32_t x = 0u; x <= xSegments; ++x)
			{
				const auto  vertex = FloorVertex{ vertexAt(x, z), glm::vec3(0.0f, 1.0f, 0.0f) };
				const auto* bytes  = reinterpret_cast<const std::byte*>(&vertex);
				mesh.vertexData.insert(mesh.vertexData.end(), bytes, bytes + c_Stride);
				submesh.aabbMin = glm::min(submesh.aabbMin, vertex.position);
				submesh.aabbMax = glm::max(submesh.aabbMax, vertex.position);
			}
		}
		submesh.vertexCount += rowStride * (zSegments + 1u);

		for (uint32_t z = 0u; z < zSegments; ++z)
		{
			for (uint32_t x0 = 0u; x0 < xSegments; x0 += c_QuadsPerMeshlet)
			{
				const uint32_t quads = std::min(c_QuadsPerMeshlet, xSegments - x0);

				auto meshlet           = assetlib::Meshlet();
				meshlet.vertexOffset   = static_cast<uint32_t>(mesh.meshletVertices.size());
				meshlet.triangleOffset = static_cast<uint32_t>(mesh.meshletTriangles.size());
				meshlet.vertexCount    = 2u * (quads + 1u);
				meshlet.triangleCount  = 2u * quads;

				for (uint32_t row = 0u; row < 2u; ++row)
				{
					for (uint32_t i = 0u; i <= quads; ++i)
					{
						mesh.meshletVertices.push_back(base + (z + row) * rowStride + x0 + i);
					}
				}

				// Local slots: the near row is 0..quads, the far row follows it.
				const auto far = quads + 1u;
				for (uint32_t near = 0u; near < quads; ++near)
				{
					for (const uint32_t slot :
					     { near, near + 1u, far + near + 1u, near, far + near + 1u, far + near })
					{
						mesh.meshletTriangles.push_back(static_cast<uint8_t>(slot));
					}
				}

				const glm::vec3 lo     = vertexAt(x0, z + 1u);
				const glm::vec3 hi     = vertexAt(x0 + quads, z);
				meshlet.boundingCenter = (lo + hi) * 0.5f;
				meshlet.boundingRadius = glm::length(hi - lo) * 0.5f;

				mesh.meshlets.push_back(meshlet);
				++submesh.meshletCount;
			}
		}
	}

	assetlib::BMesh
	MakeFloorMesh()
	{
		auto mesh = assetlib::BMesh();

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 2;
		submesh.layout.stride         = c_Stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.layout.attributes[1]  = { assetlib::VertexSemantic::kNormal,
			                              assetlib::VertexFormat::kFloat32x3,
			                              sizeof(glm::vec3) };
		submesh.aabbMin               = glm::vec3(std::numeric_limits<float>::max());
		submesh.aabbMax               = glm::vec3(std::numeric_limits<float>::lowest());
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.submeshCount = 1;
		mesh.meshes.push_back(entry);

		AppendFloor(mesh, glm::vec3(0.0f), c_FloorSize, c_FloorSegments, c_FloorSegments);
		return mesh;
	}

	bgl::SceneDesc
	FloorSceneDesc(const assetlib::BMesh& mesh)
	{
		auto sd                        = bgl::SceneDesc();
		sd.initialGeom                 = 4;
		sd.initialSubmeshes            = 4;
		sd.initialMeshlets             = static_cast<uint32_t>(mesh.meshlets.size()) + 16u;
		sd.initialVertexBufferByteSize = static_cast<uint32_t>(mesh.vertexData.size()) + 4096u;
		sd.initialIndices              = static_cast<uint32_t>(mesh.meshletTriangles.size()) + 64u;
		sd.initialPbrMaterials         = 4;
		return sd;
	}

	bgl::Camera
	FloorCamera()
	{
		auto camera = bgl::Camera();
		camera.LookAt({ 0.0f, 3.0f, 0.0f }, { 4.0f, 0.0f, -12.0f }, { 0.0f, 1.0f, 0.0f })
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_W) / static_cast<float>(c_H),
				0.1f,
				150.0f);
		return camera;
	}

	const bgl::idl::Submesh&
	OnlySubmesh(bgl::Scene& scene, bgl::GeomHandle geom)
	{
		const uint32_t root = scene.GetGeomSubmeshes(geom.handle.index).range.offsetStart;
		return scene.GetSubmeshBuffer().AtIndex(root);
	}

	enum class FloorCull
	{
		kCompacted,
		// Geometry the camera never sees, appended until the submesh has more meshlets than the
		// amplification group compacts, so the mesh stage culls instead.
		kOversize,
		// Oversize, so nothing is compacted, with every meshlet's sphere past the whole scene.
		kNothing,
	};

	/**
	 * Renders the floor, under a blob shadow so the static depth it is received on reaches the
	 * image, for enough TAA frames to walk the jitter sequence, and writes the last to `path`.
	 */
	void
	RenderFloor(const std::string& path, FloorCull cull)
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto td       = bgl::RenderTargetDesc();
		td.width      = static_cast<int>(c_W);
		td.height     = static_cast<int>(c_H);
		td.headless   = true;
		td.taaEnabled = true;
		auto target   = gfx->CreateRenderTarget(td);

		auto mesh = MakeFloorMesh();
		if (cull != FloorCull::kCompacted)
		{
			// 248 quads a row is eight full meshlets, so 1024 rows is 8192 of them.
			AppendFloor(mesh, glm::vec3(0.0f, -40.0f, 900.0f), 200.0f, 248u, 1024u);
		}
		if (cull == FloorCull::kNothing)
		{
			for (assetlib::Meshlet& meshlet : mesh.meshlets)
			{
				meshlet.boundingRadius = 1.0e6f;
			}
		}

		auto sceneRef = gfx->CreateScene(FloorSceneDesc(mesh));
		auto view     = gfx->CreateSceneView(sceneRef, 8);
		bgl::test::ApplyEnvironment(sceneRef.Get(), view.Get());

		auto* scene = sceneRef->As<bgl::Scene>();
		REQUIRE(scene != nullptr);

		// Alpha-tested and double-sided, the shape of a grass card: the pipelines that draw it are
		// the cutout ones, and StaticDepth draws it through its coverage twin.
		auto desc            = bgl::PbrMaterialDesc();
		desc.baseColorFactor = glm::vec4(0.6f, 0.8f, 0.4f, 1.0f);
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.7f;
		desc.layerType       = bgl::LayerType::kMask;
		desc.doubleSided     = true;

		const std::array<bgl::MaterialHandle, 1> materials = { sceneRef->CreatePbrMaterial(desc) };
		const auto floor = sceneRef->AddStaticMeshGeom(mesh, 0, materials);
		REQUIRE(floor.IsValid());

		const uint32_t meshlets = OnlySubmesh(*scene, floor).meshlets.count;
		INFO("meshlets " << meshlets);
		REQUIRE((meshlets > bgl::idl::cMaxCompactedMeshlets) == (cull != FloorCull::kCompacted));
		// Enough that the culling loop runs many chunks, and an odd survivor count straddles a word.
		REQUIRE(meshlets > 4u * bgl::idl::cMeshletCullGroupSize);

		view->CreateStaticMeshInstance(floor, glm::mat4(1.0f));

		const auto caster = view->CreateStaticMeshInstance(
			sceneRef->AddCubeGeom(),
			glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.6f, -9.0f)));

		auto blob       = bgl::BlobShadowDesc();
		blob.radius     = 2.0f;
		blob.intensity  = 0.9f;
		blob.fadeHeight = 3.0f;
		view->SetBlobShadow(caster, blob);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = FloorCamera();
		job.viewport = bgl::Viewport(static_cast<float>(c_W), static_cast<float>(c_H));

		for (int frame = 0; frame < 12; ++frame)
		{
			gfx->DrawFrame(target, job);
		}
		gfx->ScreenshotPng(target, path);
	}
}

TEST_CASE("Culling meshlets draws exactly what drawing every meshlet draws", "[culling][render]")
{
	const std::string unculled  = "meshlet_culling_unculled.png";
	const std::string compacted = "meshlet_culling_compacted.png";
	const std::string oversize  = "meshlet_culling_oversize.png";

	RenderFloor(unculled, FloorCull::kNothing);
	RenderFloor(compacted, FloorCull::kCompacted);
	RenderFloor(oversize, FloorCull::kOversize);

	// The premise: the floor is on screen, and so is the shadow on it.
	const bgl::test::Rgba ground = bgl::test::MeanColor(unculled, 0, c_H - 40, c_W, 40);
	CHECK(ground.Luma() > 0.05f);

	CHECK(bgl::test::MaxChannelDelta(compacted, unculled) == 0.0f);
	CHECK(bgl::test::MaxChannelDelta(oversize, unculled) == 0.0f);
}

#if defined(BERNINI_GPU_DEBUG)

TEST_CASE(
	"The static tier tests every meshlet of a visible instance and culls exactly those outside",
	"[culling][view]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	auto device          = gfxBase->GetDevice();

	auto td     = bgl::RenderTargetDesc();
	td.width    = static_cast<int>(c_W);
	td.height   = static_cast<int>(c_H);
	td.headless = true;
	auto target = gfx->CreateRenderTarget(td);

	auto* targetBase = target->As<bgl::RenderTargetBase>();
	REQUIRE(targetBase != nullptr);

	const auto mesh = MakeFloorMesh();

	auto sceneRef = gfx->CreateScene(FloorSceneDesc(mesh));
	auto viewRef  = gfx->CreateSceneView(sceneRef, 4);

	auto* scene = sceneRef->As<bgl::Scene>();
	auto* view  = viewRef->As<bgl::SceneView>();
	REQUIRE(scene != nullptr);
	REQUIRE(view != nullptr);

	// A real material kind, so the instance lands in an opaque static bucket StaticDepth draws.
	auto material         = bgl::MaterialHandle();
	material.materialType = bgl::MaterialType::kPBR;

	const std::array<bgl::MaterialHandle, 1> materials = { material };
	const auto floor = sceneRef->AddStaticMeshGeom(mesh, 0, materials);
	REQUIRE(floor.IsValid());

	// Turned and scaled, so the reference below checks the sphere is placed as the draw places it.
	const glm::mat4 transform = glm::scale(
		glm::rotate(glm::mat4(1.0f), glm::radians(20.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
		glm::vec3(1.5f));
	REQUIRE(view->CreateStaticMeshInstance(floor, transform).IsValid());

	const glm::mat4 viewProj = FloorCamera().GetViewProjection();

	// The reference cull, over the spheres the scene cooked. A sphere within a hair of a plane is
	// one the GPU may round either way, so it is counted as neither.
	const bgl::idl::Submesh& submesh      = OnlySubmesh(*scene, floor);
	const bgl::FrustumPlanes planes       = bgl::ExtractFrustumPlanes(viewProj);
	const float              scale        = 1.5f;
	uint32_t                 surelyCulled = 0u;
	uint32_t                 borderline   = 0u;
	for (uint32_t m = 0u; m < submesh.meshlets.count; ++m)
	{
		const glm::vec4 local  = scene->GetMeshletBuffer()
		                             .AtIndex(submesh.meshlets.range.offsetStart + m)
		                             .boundingSphere;
		const glm::vec3 centre = glm::vec3(transform * glm::vec4(glm::vec3(local), 1.0f));
		const float     radius = local.w * scale;

		const bool outside = !bgl::SphereIntersectsFrustum(planes, centre, radius * 1.001f);
		const bool inside  = bgl::SphereIntersectsFrustum(planes, centre, radius * 0.999f);
		surelyCulled += outside ? 1u : 0u;
		borderline += (!outside && !inside) ? 1u : 0u;
	}

	INFO("meshlets " << submesh.meshlets.count << ", surely culled " << surelyCulled);
	REQUIRE(submesh.meshlets.count <= bgl::idl::cMaxCompactedMeshlets);
	// The premise: most of the floor is off screen, and some of it is not.
	REQUIRE(surelyCulled > submesh.meshlets.count / 2u);
	REQUIRE(surelyCulled < submesh.meshlets.count);

	auto compactPass = bgl::CompactInstancesPass();
	auto depthPass   = bgl::StaticDepthPass();
	{
		const bgl::DrawBucketTable& table     = gfxBase->GetRenderContext()->DrawBuckets();
		auto                        pipelines = bgl::PipelineBatch(device);
		const auto ctx = bgl::PassInitContext{ device, &pipelines, resourceManager, &table };
		compactPass.Init(ctx);
		depthPass.Init(ctx);
		pipelines.Build();
		depthPass.CheckBindings();
	}

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = sizeof(bgl::idl::CullStats);
	rbDesc.debugName = "Cull Stats Readback";
	auto rbStats     = resourceManager->CreateReadbackBuffer(rbDesc);

	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	bgl::FrameGraph fg;
	fg.RegisterQueue("main", cmdQueue, cmdList);
	fg.ImportTexture(bgl::c_StaticDepthName, targetBase->GetStaticDepthTexture());

	fg.SetResourceNamespace(view->GetResourceNamespace());
	scene->AttachToFrameGraph(fg, 0);
	view->AttachToFrameGraph(fg, 0);

	auto draw                   = bgl::DrawData();
	draw.drawIdx                = 0;
	draw.cullIdx                = 0;
	draw.view                   = viewRef;
	draw.cullState              = &view->GetCullState(0);
	draw.viewState.viewport     = bgl::Viewport(static_cast<float>(c_W), static_cast<float>(c_H));
	draw.viewState.viewProj     = viewProj;
	draw.viewState.prevViewProj = viewProj;
	draw.viewState.cullView     = bgl::BuildCullView(viewProj);
	draw.targets.staticDepth    = targetBase->GetStaticDepthDsv();
	draw.materialArena          = scene->GetMaterialBinding();

	fg.SetResourceNamespace(view->GetCullNamespace(0));
	compactPass.AttachToFrameGraph(fg, draw);
	depthPass.AttachToFrameGraph(fg, draw);

	fg.AddPass(
		bgl::PassDesc()
			.SetName("Cull Stats Readback")
			.AddBufferArg(
				bgl::c_CullStatsName,
				bgl::BarrierSyncFlag::kCopy,
				bgl::BarrierAccessFlag::kCopySource)
			.SetSideEffect()
			.SetExec([&](const bgl::PassContext& ctx) {
				ctx.GetCommandList()->CopyBufferToReadback(
					rbStats,
					ctx.GetBuffer(bgl::c_CullStatsName));
			}));

	fg.Compile(resourceManager.Get());

	cmdList->Open(cmdQueue, cmdAllocator);
	fg.Execute();
	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* stats =
		static_cast<const bgl::idl::CullStats*>(resourceManager->MapReadback(rbStats));
	REQUIRE(stats != nullptr);

	INFO("tested " << stats->meshletsTested << ", culled " << stats->meshletsCulled);
	CHECK(stats->tested == 1u);
	CHECK(stats->frustumCulled == 0u);
	CHECK(stats->meshletsTested == submesh.meshlets.count);
	CHECK(stats->meshletsCulled >= surelyCulled);
	CHECK(stats->meshletsCulled <= surelyCulled + borderline);

	resourceManager->UnmapReadback(rbStats);
	compactPass.Release(false);
	depthPass.Release();
}

#endif
