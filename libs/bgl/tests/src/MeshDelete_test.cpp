#include "idl/idl.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
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
	[[maybe_unused]] auto& [submeshBuffer, meshletBuffer, vertexMapBuffer, vertexDataBuffer, indexBuffer, _] =
		geomBuffers;

	auto instBuffers                                              = view->GetInstanceBuffers();
	[[maybe_unused]] auto& [instanceBuffer, meshBuffer, drawArgs] = instBuffers;

	// inst.handle now refers to the per-placement Mesh record; the mesh instance owns
	// one submesh-instance per submesh (the cube has exactly one).
	const uint32_t meshIndex = inst.handle.index;
	const uint32_t geomIndex = geom.handle.index;

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

		// EntryBuffer (mesh record): live, and it tracks its source geom asset.
		CHECK(meshBuffer.IsIndexValid(meshIndex));
		CHECK(meshBuffer.MetaAt(meshIndex).geomIndex == geomIndex);
		CHECK(meshBuffer.AtIndex(meshIndex).transform[0][0] == 1.0f);
		CHECK(meshBuffer.AtIndex(meshIndex).transform[3][3] == 1.0f);

		// Geometry asset (CPU-side): live with a single reference from the instance.
		CHECK(scene->IsGeomSlotValid(geom.handle));
		CHECK(scene->GetGeomAsset(geomIndex).refCount == 1);

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

		// Geometry asset: retained, with the reference count dropped to zero.
		CHECK(scene->IsGeomSlotValid(geom.handle));
		CHECK(scene->GetGeomAsset(geomIndex).refCount == 0);

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
		CHECK_FALSE(scene->IsGeomSlotValid(geom.handle));

		// RangeBuffers: every owned range is now freed.
		CHECK_FALSE(submeshBuffer.IsIndexValid(submeshRoot));
		CHECK_FALSE(vertexDataBuffer.IsIndexValid(vertexRoot));
		CHECK_FALSE(indexBuffer.IsIndexValid(indexRoot));
		CHECK_FALSE(meshletBuffer.IsIndexValid(meshletRoot));
		CHECK_FALSE(vertexMapBuffer.IsIndexValid(vertexMapRoot));
	}
}

TEST_CASE("SetSubmeshMaterial re-selects a submesh's PSO", "[material][pso][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(CubeSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	// Material (and thus PSO) is now a per-geom property cached on the submesh; created with the
	// kNull material, the cube's submesh starts on the Null PSO.
	auto nullMat         = bgl::MaterialHandle();
	nullMat.materialType = bgl::MaterialType::kNull;

	auto geom = scene->AddCubeGeom(nullMat);
	REQUIRE(geom.IsValid());

	// The cube is the first geom, so its single submesh is at global index 0 in the Scene's
	// submesh buffer; read its cached pso directly.
	auto  geomBuffers   = scene->GetBuffers();
	auto& submeshBuffer = std::get<0>(geomBuffers);

	CHECK(
		submeshBuffer.AtIndex(0).pso ==
		static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_Null));

	SECTION("A valid material updates the submesh's PSO")
	{
		auto pbr         = bgl::MaterialHandle();
		pbr.materialType = bgl::MaterialType::kPBR;

		REQUIRE_NOTHROW(scene->SetSubmeshMaterial(geom, 0, pbr));

		CHECK(
			submeshBuffer.AtIndex(0).pso ==
			static_cast<uint32_t>(bgl::PsoType::kOpaque_StaticMesh_PBR));
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
