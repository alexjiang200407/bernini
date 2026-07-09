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
	// Feeding a count above idl::cMaxMeshletsPerAccelerationStructure makes AddStaticMesh chunk that
	// submesh across several GPU submeshes, which is the case these tests exist to pin down.
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

	// > cMaxMeshletsPerAccelerationStructure (64), so the submesh splits into two chunks.
	constexpr uint32_t c_SplitMeshletCount = 65;
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

// A source submesh with more meshlets than the per-submesh cap is chunked by AddStaticMesh into
// several GPU submeshes that SHARE one vertexData range. DeleteGeom must free that range exactly
// once -- freeing it per GPU submesh double-erased it and tripped an assertion.
TEST_CASE(
	"Deleting a mesh whose submesh split across the meshlet cap frees it once",
	"[delete][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(MeshletSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto  viewHandle = gfx->CreateSceneView(sceneHandle, 4);
	auto* view       = viewHandle->As<bgl::SceneView>();
	REQUIRE(view != nullptr);

	const std::array<uint32_t, 1> counts = { { c_SplitMeshletCount } };
	const assetlib::BMesh         mesh   = MakeMeshletMesh(counts);

	auto geom = scene->AddStaticMesh(mesh, 0, {});
	REQUIRE(geom.IsValid());

	// The single source submesh really did split into several GPU submeshes.
	CHECK(scene->GetGeomAsset(geom.handle.index).submeshes.count > 1);

	auto inst = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
	REQUIRE(inst.IsValid());
	view->DeleteMeshInstance(inst);

	REQUIRE_NOTHROW(scene->DeleteGeom(geom));

	// The freed ranges are reusable: re-adding the same mesh succeeds (the drop-the-same-mesh path).
	auto geom2 = scene->AddStaticMesh(mesh, 0, {});
	REQUIRE(geom2.IsValid());
}

// SetSubmeshMaterial is indexed by *source* submesh, but a source submesh over the meshlet cap is
// several GPU submeshes. Writing only the first left the rest of the surface on the old material --
// a mesh half-textured along a triangle-aligned seam.
TEST_CASE("SetSubmeshMaterial covers every chunk of a split submesh", "[material][pso][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(MeshletSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto  geomBuffers   = scene->GetBuffers();
	auto& submeshBuffer = std::get<0>(geomBuffers);

	auto pbr         = bgl::MaterialHandle();
	pbr.materialType = bgl::MaterialType::kPBR;

	// Asserts that source submesh `sourceIndex` of `geom` is entirely on `expected`.
	const auto checkChunksAllOn =
		[&](bgl::GeomHandle geom, uint32_t sourceIndex, bgl::PsoType expected) {
			const bgl::GeomAsset&    asset  = scene->GetGeomAsset(geom.handle.index);
			const bgl::SubmeshChunks chunks = asset.submeshChunks[sourceIndex];
			const uint32_t           root   = asset.submeshes.range.offsetStart;

			for (uint32_t i = 0; i < chunks.count; ++i)
			{
				INFO("source submesh " << sourceIndex << ", chunk " << i << " of " << chunks.count);
				CHECK(
					submeshBuffer.AtIndex(root + chunks.first + i).pso ==
					static_cast<uint32_t>(expected));
			}
		};

	SECTION("A split submesh gets the material on all of its chunks")
	{
		const std::array<uint32_t, 1> counts = { { c_SplitMeshletCount } };
		const assetlib::BMesh         mesh   = MakeMeshletMesh(counts);

		auto geom = scene->AddStaticMesh(mesh, 0, {});
		REQUIRE(geom.IsValid());

		// One source submesh, but more than one GPU submesh -- otherwise this test proves nothing.
		const bgl::GeomAsset& asset = scene->GetGeomAsset(geom.handle.index);
		REQUIRE(asset.submeshChunks.size() == 1);
		REQUIRE(asset.submeshChunks[0].count == 2);
		REQUIRE(asset.submeshes.count == 2);

		REQUIRE_NOTHROW(scene->SetSubmeshMaterial(geom, 0, pbr));
		checkChunksAllOn(geom, 0, bgl::PsoType::kOpaque_StaticMesh_PBR);
	}

	SECTION("Materialing a later submesh does not bleed into an earlier split one")
	{
		// Source submesh 0 splits into 2 chunks, so source submesh 1 lives at GPU index 2. Indexing
		// the GPU array with the source index would have written chunk 1 of submesh 0 instead.
		const std::array<uint32_t, 2> counts = { { c_SplitMeshletCount, 1 } };
		const assetlib::BMesh         mesh   = MakeMeshletMesh(counts);

		auto geom = scene->AddStaticMesh(mesh, 0, {});
		REQUIRE(geom.IsValid());

		const bgl::GeomAsset& asset = scene->GetGeomAsset(geom.handle.index);
		REQUIRE(asset.submeshChunks.size() == 2);
		REQUIRE(asset.submeshChunks[1].first == 2);
		REQUIRE(asset.submeshChunks[1].count == 1);

		REQUIRE_NOTHROW(scene->SetSubmeshMaterial(geom, 1, pbr));

		checkChunksAllOn(geom, 1, bgl::PsoType::kOpaque_StaticMesh_PBR);
		// Submesh 0 was never assigned a material, so all of its chunks stay on the Null PSO.
		checkChunksAllOn(geom, 0, bgl::PsoType::kOpaque_StaticMesh_Null);
	}

	SECTION("The index is a source submesh index, so one past the last source submesh throws")
	{
		const std::array<uint32_t, 1> counts = { { c_SplitMeshletCount } };
		const assetlib::BMesh         mesh   = MakeMeshletMesh(counts);

		auto geom = scene->AddStaticMesh(mesh, 0, {});
		REQUIRE(geom.IsValid());

		// There are 2 GPU submeshes but only 1 source submesh; index 1 must be rejected.
		REQUIRE_THROWS_AS(scene->SetSubmeshMaterial(geom, 1, pbr), bgl::SceneError);
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
