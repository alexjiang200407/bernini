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
#include <bgl_common/idl/MeshletGroup.h>
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
// that against the same floor drawn with nothing culled -- every sphere inflated past the whole
// scene, so the same shaders dispatch and keep every group -- through both halves of the cull: the
// amplification group's per-group test, and the mesh stage's own test of the meshlet it draws. The
// second pins that culling happens at all, which an image cannot show.

namespace
{
	constexpr uint32_t c_W = 256;
	constexpr uint32_t c_H = 160;

	// Wider than the frustum on every side and deeper than its far plane, and behind the camera too,
	// so every plane of the frustum cuts it.
	// Odd on purpose: at c_QuadsPerMeshlet a column is 97 meshlets and there are 14 of them, so the
	// floor is 1358 -- not a multiple of cMeshletsPerGroup, which is what puts a short last group in
	// every case below.
	constexpr uint32_t c_FloorSegments = 97;
	constexpr float    c_FloorSize     = 400.0f;

	struct FloorVertex
	{
		glm::vec3 position;
		glm::vec3 normal;
	};

	constexpr uint16_t c_Stride = sizeof(FloorVertex);

	// A strip of up to this many quads is one meshlet -- far short of the 64 vertices one holds,
	// because the meshlets below are emitted a column at a time, so a run of cMeshletsPerGroup of
	// them is a compact block the way a cook's clusters are. A meshlet spanning a whole row would
	// give its group a bound reaching the frustum wherever the camera looked.
	constexpr uint32_t c_QuadsPerMeshlet = 7;

	/**
	 * Appends a horizontal grid facing +Y, centred on `centre`, to `mesh`'s one submesh, cut into
	 * meshlets a row strip at a time -- a column of strips before the next column, so consecutive
	 * meshlets are neighbours -- with bounds measured the way the cook's are, enclosing every vertex,
	 * and appended after the meshlets already there.
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

		for (uint32_t x0 = 0u; x0 < xSegments; x0 += c_QuadsPerMeshlet)
		{
			for (uint32_t z = 0u; z < zSegments; ++z)
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

	/**
	 * Fills the mesh's cooked group bounds from the meshlet spheres as they now stand, one per run
	 * of `cMeshletsPerGroup`. Run last, so a case that has moved a meshlet's sphere moves the bound
	 * over it too.
	 */
	void
	FillMeshletGroups(assetlib::BMesh& mesh)
	{
		assetlib::Submesh& submesh = mesh.submeshes.front();

		submesh.firstMeshletGroup = 0;
		mesh.meshletGroups.clear();

		for (uint32_t first = 0; first < submesh.meshletCount;
		     first += assetlib::c_MeshletsPerGroup)
		{
			const uint32_t last =
				std::min(first + assetlib::c_MeshletsPerGroup, submesh.meshletCount);

			auto lo = glm::vec3(std::numeric_limits<float>::max());
			auto hi = glm::vec3(std::numeric_limits<float>::lowest());
			for (uint32_t m = first; m < last; ++m)
			{
				const assetlib::Meshlet& meshlet = mesh.meshlets[submesh.firstMeshlet + m];
				lo = glm::min(lo, meshlet.boundingCenter - meshlet.boundingRadius);
				hi = glm::max(hi, meshlet.boundingCenter + meshlet.boundingRadius);
			}

			auto group           = assetlib::MeshletGroup();
			group.boundingCenter = (lo + hi) * 0.5f;
			group.boundingRadius = 0.0f;
			for (uint32_t m = first; m < last; ++m)
			{
				const assetlib::Meshlet& meshlet = mesh.meshlets[submesh.firstMeshlet + m];
				group.boundingRadius             = std::max(
					group.boundingRadius,
					glm::distance(group.boundingCenter, meshlet.boundingCenter) +
						meshlet.boundingRadius);
			}

			mesh.meshletGroups.push_back(group);
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
		FillMeshletGroups(mesh);
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
		// payload once held bits for, so the group bound is what makes it compact at all.
		kLarge,
		// Large, with every sphere -- the meshlets' and the groups' over them -- past the whole
		// scene, so nothing is culled at either level.
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
			// 252 quads a row is 36 full meshlets, so 200 rows is 7200 of them -- enough that the
			// submesh holds more meshlets than the payload has bits for groups.
			AppendFloor(mesh, glm::vec3(0.0f, -40.0f, 900.0f), 200.0f, 252u, 200u);
		}
		if (cull == FloorCull::kNothing)
		{
			for (assetlib::Meshlet& meshlet : mesh.meshlets)
			{
				meshlet.boundingRadius = 1.0e6f;
			}
		}
		FillMeshletGroups(mesh);

		auto sceneRef = gfx->CreateScene(FloorSceneDesc(mesh));
		auto view     = gfx->CreateSceneView(sceneRef, 8);
		bgl::test::ApplyEnvironment(sceneRef.Get(), view.Get());

		auto* scene = sceneRef->As<bgl::Scene>();
		REQUIRE(scene != nullptr);

		// Alpha-tested and double-sided, the shape of a grass card: the pipelines that draw it are
		// the cutout ones.
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
		const uint32_t groups =
			(meshlets + bgl::idl::cMeshletsPerGroup - 1u) / bgl::idl::cMeshletsPerGroup;

		INFO("meshlets " << meshlets << ", groups " << groups);
		// The large cases hold more meshlets than the payload has bits, so only the group bound
		// makes them compact -- and every case has a last group the submesh does not fill.
		REQUIRE((meshlets > bgl::idl::cMaxCompactedGroups) == (cull != FloorCull::kCompacted));
		REQUIRE(groups <= bgl::idl::cMaxCompactedGroups);
		REQUIRE(meshlets % bgl::idl::cMeshletsPerGroup != 0u);
		// Enough that the culling loop runs many chunks, and an odd survivor count straddles a word.
		REQUIRE(groups > 2u * bgl::idl::cMeshletCullGroupSize);

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
	const std::string large     = "meshlet_culling_large.png";

	RenderFloor(unculled, FloorCull::kNothing);
	RenderFloor(compacted, FloorCull::kCompacted);
	RenderFloor(large, FloorCull::kLarge);

	// The premise: the floor is on screen, and so is the shadow on it.
	const bgl::test::Rgba ground = bgl::test::MeanColor(unculled, 0, c_H - 40, c_W, 40);
	CHECK(ground.Luma() > 0.05f);

	CHECK(bgl::test::MaxChannelDelta(compacted, unculled) == 0.0f);
	CHECK(bgl::test::MaxChannelDelta(large, unculled) == 0.0f);
}

#if defined(BERNINI_GPU_DEBUG)

TEST_CASE(
	"The static tier tests every meshlet group of a visible instance and culls exactly those "
	"outside",
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

	// kNull: an opaque static bucket whose pixel stage reads no lighting, which this graph binds
	// none of. The amplification stage under test is the same for every static bucket.
	auto material         = bgl::MaterialHandle();
	material.materialType = bgl::MaterialType::kNull;

	const std::array<bgl::MaterialHandle, 1> materials = { material };
	const auto floor = sceneRef->AddStaticMeshGeom(mesh, 0, materials);
	REQUIRE(floor.IsValid());

	// Turned and scaled, so the reference below checks the sphere is placed as the draw places it.
	const glm::mat4 transform = glm::scale(
		glm::rotate(glm::mat4(1.0f), glm::radians(20.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
		glm::vec3(1.5f));
	REQUIRE(view->CreateStaticMeshInstance(floor, transform).IsValid());

	const glm::mat4 viewProj = FloorCamera().GetViewProjection();

	// The reference cull, over the group bounds the scene holds -- what the amplification stage
	// tests, one per cMeshletsPerGroup meshlets. A sphere within a hair of a plane is one the GPU
	// may round either way, so it is counted as neither.
	const bgl::idl::Submesh& submesh = OnlySubmesh(*scene, floor);
	const uint32_t           groups =
		(submesh.meshlets.count + bgl::idl::cMeshletsPerGroup - 1u) / bgl::idl::cMeshletsPerGroup;

	const bgl::FrustumPlanes planes       = bgl::ExtractFrustumPlanes(viewProj);
	const float              scale        = 1.5f;
	uint32_t                 surelyCulled = 0u;
	uint32_t                 borderline   = 0u;
	for (uint32_t g = 0u; g < groups; ++g)
	{
		const glm::vec4 local  = scene->GetMeshletGroupBuffer()
		                             .AtIndex(submesh.meshletGroups.offsetStart + g)
		                             .boundingSphere;
		const glm::vec3 centre = glm::vec3(transform * glm::vec4(glm::vec3(local), 1.0f));
		const float     radius = local.w * scale;

		const bool outside = !bgl::SphereIntersectsFrustum(planes, centre, radius * 1.001f);
		const bool inside  = bgl::SphereIntersectsFrustum(planes, centre, radius * 0.999f);
		surelyCulled += outside ? 1u : 0u;
		borderline += (!outside && !inside) ? 1u : 0u;
	}

	INFO("groups " << groups << ", surely culled " << surelyCulled);
	REQUIRE(groups <= bgl::idl::cMaxCompactedGroups);
	// The premise: most of the floor is off screen, and some of it is not.
	REQUIRE(surelyCulled > groups / 2u);
	REQUIRE(surelyCulled < groups);

	auto compactPass   = bgl::CompactInstancesPass();
	auto forwardPhases = bgl::ForwardPhases();
	{
		const bgl::DrawBucketTable& table     = gfxBase->GetRenderContext()->DrawBuckets();
		auto                        pipelines = bgl::PipelineBatch(device);
		const auto ctx = bgl::PassInitContext{ device, &pipelines, resourceManager, &table };
		compactPass.Init(ctx);
		forwardPhases.Init(ctx);
		forwardPhases.AddDrawBucketKernels(ctx, view->DemandedDrawBuckets());
		pipelines.Build();
		forwardPhases.CheckBindings();
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
	fg.ImportTexture(bgl::c_BackbufferName, targetBase->GetSceneColorTexture());
	fg.ImportTexture(bgl::c_MotionVectorsName, targetBase->GetMotionVectorTexture());
	fg.ImportTexture(bgl::c_DepthName, targetBase->GetDepthTexture());

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
	draw.targets.sceneColor     = targetBase->GetSceneColorRtv();
	draw.targets.motionVector   = targetBase->GetMotionVectorRtv();
	draw.targets.depth          = targetBase->GetDepthDsv();
	draw.materialArena          = scene->GetMaterialBinding();

	fg.SetResourceNamespace(view->GetCullNamespace(0));
	compactPass.AttachToFrameGraph(fg, draw);
	forwardPhases.AttachToFrameGraph(fg, draw, bgl::ForwardPhase::kWorld);

	fg.AddPass(
		bgl::PassDesc()
			.SetName("Cull Stats Readback")
			.AddCopySource(bgl::c_CullStatsName)
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

	INFO("tested " << stats->meshletGroupsTested << ", culled " << stats->meshletGroupsCulled);
	CHECK(stats->tested == 1u);
	CHECK(stats->frustumCulled == 0u);
	CHECK(stats->meshletGroupsTested == groups);
	CHECK(stats->meshletGroupsCulled >= surelyCulled);
	CHECK(stats->meshletGroupsCulled <= surelyCulled + borderline);

	resourceManager->UnmapReadback(rbStats);
	compactPass.Release(false);
	forwardPhases.Release();
}

#endif
