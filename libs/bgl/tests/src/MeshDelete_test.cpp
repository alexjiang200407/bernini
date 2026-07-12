#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "idl/idl.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/util.h"
#include <bgl/IGraphics.h>
#include <bgl/PsoType.h>

namespace
{
	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.enableDebugLayer = false;
		return opts;
	}

	bgl::SceneDesc
	CubeSceneDesc()
	{
		auto desc                    = bgl::SceneDesc();
		desc.maxGeom                 = 5;
		desc.maxSubmeshes            = 5;
		desc.maxMeshlets             = 100;
		desc.maxVertexBufferByteSize = 40000;
		desc.maxVertexBufferByteSize = 32000;
		desc.maxIndices              = 1000;
		return desc;
	}

	bgl::SceneDesc
	MeshletSceneDesc()
	{
		auto desc                    = bgl::SceneDesc();
		desc.maxGeom                 = 4;
		desc.maxSubmeshes            = 16;
		desc.maxMeshlets             = 256;
		desc.maxVertexBufferByteSize = 64000;
		desc.maxIndices              = 4000;
		return desc;
	}

	// A BMesh with one source submesh per entry of `meshletCounts`, each meshlet a single triangle.
	// A submesh's meshlet count is a dispatch dimension, never a partitioning criterion, so however
	// large a count is fed in, AddStaticMesh must still produce exactly one GPU submesh for it.
	assetlib::BMesh
	MakeMeshletMesh(std::span<const uint32_t> meshletCounts)
	{
		constexpr uint16_t kStride = 12;  // one float32x3 position

		auto mesh = assetlib::BMesh();
		mesh.stringPool.push_back('\0');

		uint32_t totalVertices = 0;
		for (const uint32_t count : meshletCounts) totalVertices += count * 3;
		mesh.vertexData.resize(static_cast<size_t>(totalVertices) * kStride);

		uint32_t vertexCursor = 0;
		for (const uint32_t count : meshletCounts)
		{
			const auto firstMeshlet = static_cast<uint32_t>(mesh.meshlets.size());

			for (uint32_t i = 0; i < count; ++i)
			{
				auto meshlet           = assetlib::Meshlet();
				meshlet.vertexOffset   = static_cast<uint32_t>(mesh.meshletVertices.size());
				meshlet.triangleOffset = static_cast<uint32_t>(mesh.meshletTriangles.size());
				meshlet.vertexCount    = 3;
				meshlet.triangleCount  = 1;
				meshlet.boundingCenter = glm::vec3(0.0f);
				meshlet.boundingRadius = 1.0f;
				mesh.meshlets.push_back(meshlet);

				// vertexMap entries are submesh-local vertex indices, not global ones.
				for (uint32_t v = 0; v < 3; ++v) mesh.meshletVertices.push_back(i * 3 + v);
				for (uint8_t t = 0; t < 3; ++t) mesh.meshletTriangles.push_back(t);
			}

			auto submesh                  = assetlib::Submesh();
			submesh.layout.attributeCount = 1;
			submesh.layout.stride         = kStride;
			submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
				                              assetlib::VertexFormat::kFloat32x3,
				                              0 };
			submesh.vertexByteOffset      = vertexCursor * kStride;
			submesh.vertexCount           = count * 3;
			submesh.firstMeshlet          = firstMeshlet;
			submesh.meshletCount          = count;
			submesh.material              = assetlib::c_InvalidIndex;
			submesh.aabbMin               = glm::vec3(-1.0f);
			submesh.aabbMax               = glm::vec3(1.0f);
			submesh.nameOffset            = 0;
			mesh.submeshes.push_back(submesh);

			vertexCursor += count * 3;
		}

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = static_cast<uint32_t>(meshletCounts.size());
		entry.nameOffset   = 0;
		mesh.meshes.push_back(entry);

		return mesh;
	}

	// Comfortably more meshlets than a mesh-shader thread group has threads (64): a submesh this
	// size used to be chunked into several GPU submeshes.
	constexpr uint32_t c_LargeMeshletCount = 65;
}

TEST_CASE("Buffer contents around mesh deletion", "[delete][buffers][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto sceneHandle = gfx->CreateScene(CubeSceneDesc());
	REQUIRE(sceneHandle != nullptr);

	auto* scene = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto viewHandle = gfx->CreateSceneView(sceneHandle, 5);
	REQUIRE(viewHandle != nullptr);

	auto* view = viewHandle->As<bgl::SceneView>();
	REQUIRE(view != nullptr);

	auto material         = bgl::MaterialHandle();
	material.materialType = bgl::MaterialType::kPBR;

	auto geom = scene->AddCubeGeom(material);
	REQUIRE(geom.IsValid());

	auto inst = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
	REQUIRE(inst.IsValid());

	// Geometry range buffers live on the Scene; instance buffers on the SceneView.
	auto geomBuffers = scene->GetBuffers();
	[[maybe_unused]] auto& [submeshBuffer, meshletBuffer, vertexMapBuffer, vertexDataBuffer, indexBuffer, pbrBuffer, looseBuffer] =
		geomBuffers;

	auto instBuffers                                              = view->GetInstanceBuffers();
	[[maybe_unused]] auto& [instanceBuffer, meshBuffer, drawArgs] = instBuffers;

	// inst.handle now refers to the per-placement Mesh record; the mesh instance owns
	// one submesh-instance per submesh (the cube has exactly one).
	const uint32_t meshIndex = inst.handle.index;

	REQUIRE(meshBuffer.MetaAt(meshIndex).submeshInstances.size() == 1);
	const auto submeshInstance = meshBuffer.MetaAt(meshIndex).submeshInstances[0];

	// The per-placement Mesh (owned by the view) carries the submeshes descriptor.
	const uint32_t submeshRoot = meshBuffer.AtIndex(meshIndex).submeshes.range.offsetStart;

	const auto&    submesh       = submeshBuffer.AtIndex(submeshRoot);
	const uint32_t vertexRoot    = submesh.vertexData.offsetStart;
	const uint32_t indexRoot     = submesh.indices.offsetStart;
	const uint32_t meshletRoot   = submesh.meshlets.range.offsetStart;
	const uint32_t vertexMapRoot = submesh.vertexMap.offsetStart;

	SECTION("Contents before deletion")
	{
		// PackedBuffer: one live submesh-instance, reachable through its handle.
		CHECK(instanceBuffer.Count() == 1);
		CHECK(instanceBuffer.IsValid(submeshInstance));

		// EntryBuffer (mesh record): live. It no longer records its source geom -- an instance
		// references geometry only by the submesh range it copied.
		CHECK(meshBuffer.IsIndexValid(meshIndex));
		CHECK(meshBuffer.AtIndex(meshIndex).transform[0][0] == 1.0f);
		CHECK(meshBuffer.AtIndex(meshIndex).transform[3][3] == 1.0f);

		// Geometry (CPU-side): alive. Nothing counts the instances referencing it.
		CHECK(scene->IsGeomAlive(geom));

		// RangeBuffers: the cube's geometry data is live.
		CHECK(submeshBuffer.IsIndexValid(submeshRoot));
		CHECK(vertexDataBuffer.IsIndexValid(vertexRoot));
		CHECK(indexBuffer.IsIndexValid(indexRoot));
		CHECK(meshletBuffer.IsIndexValid(meshletRoot));
		CHECK(vertexMapBuffer.IsIndexValid(vertexMapRoot));
	}

	SECTION("After DeleteMeshInstance the geom and its ranges survive")
	{
		view->DeleteMeshInstance(inst);

		// PackedBuffer: the submesh-instance entry is gone and its handle is now stale.
		CHECK(instanceBuffer.Count() == 0);
		CHECK(instanceBuffer.IsEmpty());
		CHECK_FALSE(instanceBuffer.IsValid(submeshInstance));

		// EntryBuffer (mesh record): its slot was freed.
		CHECK_FALSE(meshBuffer.IsIndexValid(meshIndex));

		// Geometry: retained. Deleting an instance never frees geometry.
		CHECK(scene->IsGeomAlive(geom));

		// RangeBuffers: untouched - deleting a mesh does not free geometry.
		CHECK(submeshBuffer.IsIndexValid(submeshRoot));
		CHECK(vertexDataBuffer.IsIndexValid(vertexRoot));
		CHECK(indexBuffer.IsIndexValid(indexRoot));
		CHECK(meshletBuffer.IsIndexValid(meshletRoot));
		CHECK(vertexMapBuffer.IsIndexValid(vertexMapRoot));
	}

	SECTION("After DeleteGeom the geometry ranges are freed")
	{
		view->DeleteMeshInstance(inst);
		scene->DeleteGeom(geom);

		// Geometry asset: slot freed, handle stale.
		CHECK_FALSE(scene->IsGeomAlive(geom));

		// RangeBuffers: every owned range is now freed.
		CHECK_FALSE(submeshBuffer.IsIndexValid(submeshRoot));
		CHECK_FALSE(vertexDataBuffer.IsIndexValid(vertexRoot));
		CHECK_FALSE(indexBuffer.IsIndexValid(indexRoot));
		CHECK_FALSE(meshletBuffer.IsIndexValid(meshletRoot));
		CHECK_FALSE(vertexMapBuffer.IsIndexValid(vertexMapRoot));
	}
}

// A submesh is the unit of pipeline state, so its meshlet count -- however large -- must not split
// it. AddStaticMesh once chunked a submesh past 64 meshlets into several GPU submeshes, which made
// the source and GPU submesh indices disagree and left the chunks sharing one vertexData range.
TEST_CASE("A submesh maps 1:1 to a GPU submesh whatever its meshlet count", "[scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(MeshletSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto  viewHandle = gfx->CreateSceneView(sceneHandle, 4);
	auto* view       = viewHandle->As<bgl::SceneView>();
	REQUIRE(view != nullptr);

	const std::array<uint32_t, 2> counts = { { c_LargeMeshletCount, 1 } };
	const assetlib::BMesh         mesh   = MakeMeshletMesh(counts);

	auto geom = scene->AddStaticMesh(mesh, 0, {});
	REQUIRE(geom.IsValid());

	// Two source submeshes in, two GPU submeshes out -- the 65-meshlet one did not split.
	CHECK(scene->GetGeomSubmeshes(geom.handle.index).count == 2u);

	auto           geomBuffers   = scene->GetBuffers();
	auto&          submeshBuffer = std::get<0>(geomBuffers);
	const uint32_t root          = scene->GetGeomSubmeshes(geom.handle.index).range.offsetStart;

	// The whole source submesh is dispatched from one GPU submesh, so all of its meshlets are there.
	CHECK(submeshBuffer.AtIndex(root).meshlets.count == c_LargeMeshletCount);
	CHECK(submeshBuffer.AtIndex(root + 1).meshlets.count == 1);

	// Each submesh owns its own vertexData range; nothing is shared, so nothing is double-freed.
	CHECK(
		submeshBuffer.AtIndex(root).vertexData.offsetStart !=
		submeshBuffer.AtIndex(root + 1).vertexData.offsetStart);

	auto inst = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
	REQUIRE(inst.IsValid());
	view->DeleteMeshInstance(inst);

	REQUIRE_NOTHROW(scene->DeleteGeom(geom));

	// The freed ranges are reusable: re-adding the same mesh succeeds (the drop-the-same-mesh path).
	auto geom2 = scene->AddStaticMesh(mesh, 0, {});
	REQUIRE(geom2.IsValid());
}

// SetSubmeshMaterial is indexed by source submesh. Once chunking made that index disagree with the
// GPU submesh index, materialing one submesh wrote over a neighbour's, or covered only part of its
// own -- a mesh half-textured along a triangle-aligned seam.
TEST_CASE("SetSubmeshMaterial addresses submeshes by source index", "[material][pso][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(MeshletSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto pbr         = bgl::MaterialHandle();
	pbr.materialType = bgl::MaterialType::kPBR;

	// The submesh's *default* material -- the PSO is no longer cached on the GPU submesh, it is
	// resolved from this onto each SubmeshInstance. Checking the default is checking what every
	// non-overridden instance of this geom will bucket into.
	const auto checkPso = [&](bgl::GeomHandle geom, uint32_t sourceIndex, bgl::PsoType expected) {
		const bgl::idl::RangeWithCount& submeshes = scene->GetGeomSubmeshes(geom.handle.index);
		INFO("source submesh " << sourceIndex);
		CHECK(
			bgl::SubmeshPso(
				bgl::GeomType::kStaticMesh,
				scene->GetSubmeshDefaultMaterial(submeshes.range.offsetStart, sourceIndex)) ==
			static_cast<uint32_t>(expected));
	};

	// Source submesh 0 has 65 meshlets, which chunking would have expanded into two GPU submeshes,
	// pushing source submesh 1 to GPU index 2.
	const std::array<uint32_t, 2> counts = { { c_LargeMeshletCount, 1 } };
	const assetlib::BMesh         mesh   = MakeMeshletMesh(counts);

	SECTION("Materialing a submesh covers it and leaves its neighbour alone")
	{
		auto geom = scene->AddStaticMesh(mesh, 0, {});
		REQUIRE(geom.IsValid());

		REQUIRE_NOTHROW(scene->SetSubmeshMaterial(geom, 1, pbr));

		checkPso(geom, 1, bgl::PsoType::kOpaque_StaticMesh_PBR);
		// Submesh 0 was never assigned a material, so it stays on the Null PSO.
		checkPso(geom, 0, bgl::PsoType::kOpaque_StaticMesh_Null);
	}

	SECTION("One past the last source submesh throws")
	{
		auto geom = scene->AddStaticMesh(mesh, 0, {});
		REQUIRE(geom.IsValid());

		REQUIRE_THROWS_AS(scene->SetSubmeshMaterial(geom, 2, pbr), bgl::SceneError);
	}
}

TEST_CASE("SetSubmeshMaterial re-selects a submesh's PSO", "[material][pso][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(CubeSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	// The default material of a geom's submesh is what its instances resolve their PSO from; created
	// with the kNull material, the cube's submesh defaults to the Null PSO.
	auto nullMat         = bgl::MaterialHandle();
	nullMat.materialType = bgl::MaterialType::kNull;

	auto geom = scene->AddCubeGeom(nullMat);
	REQUIRE(geom.IsValid());

	// The cube is the first geom, so its single submesh is at global index 0.
	const auto psoOfDefault = [&]() {
		return bgl::SubmeshPso(bgl::GeomType::kStaticMesh, scene->GetSubmeshDefaultMaterial(0, 0));
	};

	CHECK(psoOfDefault() == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_Null));

	SECTION("A valid material re-selects the submesh's PSO, and bumps the epoch")
	{
		auto pbr         = bgl::MaterialHandle();
		pbr.materialType = bgl::MaterialType::kPBR;

		const uint64_t before = scene->MaterialEpoch();

		REQUIRE_NOTHROW(scene->SetSubmeshMaterial(geom, 0, pbr));

		CHECK(psoOfDefault() == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_PBR));

		// The epoch is what carries the change to instances placed before it: a SceneView polls it in
		// Update and re-resolves. Without the bump, a live instance would keep its stale PSO forever.
		CHECK(scene->MaterialEpoch() != before);
	}

	SECTION("Out-of-range submesh index throws")
	{
		auto pbr         = bgl::MaterialHandle();
		pbr.materialType = bgl::MaterialType::kPBR;

		REQUIRE_THROWS_AS(scene->SetSubmeshMaterial(geom, 1, pbr), bgl::SceneError);
	}

	SECTION("An invalid material throws")
	{
		auto bad         = bgl::MaterialHandle();
		bad.materialType = bgl::MaterialType::kInvalid;

		REQUIRE_THROWS_AS(scene->SetSubmeshMaterial(geom, 0, bad), bgl::SceneError);
	}

	SECTION("An invalid geom handle throws")
	{
		auto pbr         = bgl::MaterialHandle();
		pbr.materialType = bgl::MaterialType::kPBR;

		REQUIRE_THROWS_AS(scene->SetSubmeshMaterial(bgl::GeomHandle{}, 0, pbr), bgl::SceneError);
	}
}

// The PSO is resolved onto the instance at placement time, so a default-material change made *after*
// an instance exists has to reach it somehow. It does not do so by a push -- the Scene deliberately
// keeps no record of who placed what -- but by a pull: SetSubmeshMaterial bumps the Scene's material
// epoch, and the SceneView re-resolves in its next Update. Without that, an instance placed before
// the change would keep drawing with the stale PSO forever, which is a silent wrong-pixel-shader bug
// rather than a crash.
TEST_CASE("A live instance re-resolves its PSO after SetSubmeshMaterial", "[material][pso][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(CubeSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto  viewHandle = gfx->CreateSceneView(sceneHandle, 4);
	auto* view       = viewHandle->As<bgl::SceneView>();
	REQUIRE(view != nullptr);

	auto nullMat         = bgl::MaterialHandle();
	nullMat.materialType = bgl::MaterialType::kNull;

	auto geom = scene->AddCubeGeom(nullMat);
	REQUIRE(geom.IsValid());

	// Placed *before* the material changes: this is the instance the epoch has to reach.
	auto inst = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
	REQUIRE(inst.IsValid());

	auto  instBuffers    = view->GetInstanceBuffers();
	auto& instanceBuffer = std::get<0>(instBuffers);
	auto& meshBuffer     = std::get<1>(instBuffers);

	const auto& meta = meshBuffer.MetaAt(inst.handle.index);
	REQUIRE(meta.submeshInstances.size() == 1);
	const auto submeshInstance = meta.submeshInstances[0];

	const auto instancePso = [&]() { return instanceBuffer[submeshInstance].pso; };

	// It resolved off the geom's default at placement time.
	CHECK(instancePso() == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_Null));

	// SceneView::Update is where the pull happens, and it flushes dirty blocks, so it needs a real
	// command list to record the upload into.
	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto device       = gfxBase->GetDevice();
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	auto cmdListDesc = bgl::CommandListDesc();
	cmdListDesc.type = bgl::QueueType::kGraphics;

	auto cmdList =
		device->CreateCommandList(cmdListDesc, cmdAllocator, gfxBase->GetResourceManagerCpy());

	const auto pumpUpdate = [&]() {
		cmdList->Open(cmdQueue, cmdAllocator);
		view->Update(cmdList);
		cmdList->Close();
		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));
	};

	// An Update with nothing changed must not disturb it.
	pumpUpdate();
	CHECK(instancePso() == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_Null));

	auto pbr         = bgl::MaterialHandle();
	pbr.materialType = bgl::MaterialType::kPBR;

	auto cutout         = bgl::MaterialHandle();
	cutout.materialType = bgl::MaterialType::kPBR;
	cutout.layerType    = bgl::LayerType::kAlphaTest;

	SECTION("the change reaches an instance placed before it")
	{
		scene->SetSubmeshMaterial(geom, 0, pbr);

		// Not until the view runs: the Scene cannot reach into a view it does not know exists.
		CHECK(instancePso() == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_Null));

		pumpUpdate();
		CHECK(instancePso() == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_PBR));
	}

	SECTION("the layer type moves the instance to a different PSO bucket")
	{
		// Same material *type*, different layer -- so this only lands in the right bucket if the
		// resolve keeps the whole MaterialHandle rather than just its buffer index.
		scene->SetSubmeshMaterial(geom, 0, cutout);
		pumpUpdate();

		CHECK(instancePso() == static_cast<uint32_t>(bgl::PsoType::kAlphaTest_StaticMesh_PBR));
	}

	SECTION("an instance placed after the change is already current")
	{
		scene->SetSubmeshMaterial(geom, 0, pbr);

		auto later = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
		REQUIRE(later.IsValid());

		const auto& laterMeta = meshBuffer.MetaAt(later.handle.index);
		REQUIRE(laterMeta.submeshInstances.size() == 1);

		CHECK(
			instanceBuffer[laterMeta.submeshInstances[0]].pso ==
			static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_PBR));

		// ...and the older one still catches up on the next Update, rather than being stranded by the
		// newer placement having already advanced the view's epoch.
		pumpUpdate();
		CHECK(instancePso() == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_PBR));
	}
}

// The whole point of moving the material onto the instance: two instances of one geom, wearing
// different materials, bucketed into different PSOs. A skin.
TEST_CASE("A material override changes one instance and not its siblings", "[material][pso][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(CubeSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto  viewHandle = gfx->CreateSceneView(sceneHandle, 4);
	auto* view       = viewHandle->As<bgl::SceneView>();
	REQUIRE(view != nullptr);

	auto pbr         = bgl::MaterialHandle();
	pbr.materialType = bgl::MaterialType::kPBR;

	// A cutout: same material *type*, different layer, so it lands in a different PSO bucket.
	auto cutout         = bgl::MaterialHandle();
	cutout.materialType = bgl::MaterialType::kPBR;
	cutout.layerType    = bgl::LayerType::kAlphaTest;

	auto geom = scene->AddCubeGeom(pbr);
	REQUIRE(geom.IsValid());

	auto worn  = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
	auto plain = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
	REQUIRE(worn.IsValid());
	REQUIRE(plain.IsValid());

	auto  instBuffers    = view->GetInstanceBuffers();
	auto& instanceBuffer = std::get<0>(instBuffers);
	auto& meshBuffer     = std::get<1>(instBuffers);

	const auto psoOf = [&](bgl::MeshInstanceHandle instance) {
		const auto& meta = meshBuffer.MetaAt(instance.handle.index);
		return instanceBuffer[meta.submeshInstances[0]].pso;
	};

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto device       = gfxBase->GetDevice();
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	auto cmdListDesc = bgl::CommandListDesc();
	cmdListDesc.type = bgl::QueueType::kGraphics;

	auto cmdList =
		device->CreateCommandList(cmdListDesc, cmdAllocator, gfxBase->GetResourceManagerCpy());

	const auto pumpUpdate = [&]() {
		cmdList->Open(cmdQueue, cmdAllocator);
		view->Update(cmdList);
		cmdList->Close();
		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));
	};

	// Both start on the geom's default.
	CHECK(psoOf(worn) == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_PBR));
	CHECK(psoOf(plain) == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_PBR));

	SECTION("the override takes effect immediately, on that instance alone")
	{
		view->SetSubmeshMaterialOverride(worn, 0, cutout);

		CHECK(psoOf(worn) == static_cast<uint32_t>(bgl::PsoType::kAlphaTest_StaticMesh_PBR));
		CHECK(psoOf(plain) == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_PBR));
	}

	SECTION("clearing it returns that instance to the default")
	{
		view->SetSubmeshMaterialOverride(worn, 0, cutout);
		view->ClearSubmeshMaterialOverride(worn, 0);

		CHECK(psoOf(worn) == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_PBR));
	}

	SECTION("an override outranks a later change to the geom's default")
	{
		view->SetSubmeshMaterialOverride(worn, 0, cutout);

		auto nullMat         = bgl::MaterialHandle();
		nullMat.materialType = bgl::MaterialType::kNull;
		scene->SetSubmeshMaterial(geom, 0, nullMat);

		// The epoch re-resolve must skip the overridden instance and rewrite only its sibling.
		pumpUpdate();

		CHECK(psoOf(worn) == static_cast<uint32_t>(bgl::PsoType::kAlphaTest_StaticMesh_PBR));
		CHECK(psoOf(plain) == static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_Null));
	}

	SECTION("a bad handle or index throws")
	{
		REQUIRE_THROWS_AS(view->SetSubmeshMaterialOverride(worn, 1, cutout), bgl::SceneError);
		REQUIRE_THROWS_AS(
			view->SetSubmeshMaterialOverride(bgl::MeshInstanceHandle{}, 0, cutout),
			bgl::SceneError);
		REQUIRE_THROWS_AS(
			view->SetSubmeshMaterialOverride(worn, 0, bgl::MaterialHandle{}),
			bgl::SceneError);
		REQUIRE_THROWS_AS(view->ClearSubmeshMaterialOverride(worn, 1), bgl::SceneError);
	}
}
