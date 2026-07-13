#include "scene/Scene.h"
#include <bgl/IGraphics.h>

namespace
{
	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.enableDebugLayer = false;
		return opts;
	}

	// One source submesh per entry of `meshletCounts`, each meshlet a single triangle. A submesh of
	// N meshlets carries 3N vertices of 12 bytes, so its vertex data is 36N bytes.
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

	// Room for the small mesh several times over, but only 900 bytes (225 words) of vertex data --
	// less than the 1800 bytes the big mesh below needs. So the big one is rejected at the vertex
	// buffer, *after* it has taken its meshlets and vertex map.
	bgl::SceneDesc
	TightVertexBudget()
	{
		auto desc                    = bgl::SceneDesc();
		desc.maxGeom                 = 8;
		desc.maxSubmeshes            = 8;
		desc.maxMeshlets             = 60;
		desc.maxVertexBufferByteSize = 900;
		desc.maxIndices              = 1000;
		return desc;
	}

	constexpr uint32_t c_BigMeshlets   = 50;  // 1800 bytes of vertex data: over the budget
	constexpr uint32_t c_SmallMeshlets = 20;  //  720 bytes, and 20 <= maxMeshlets: fits easily
}

TEST_CASE("A mesh too big for the scene's budgets is rejected", "[scene][capacity][overflow]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(TightVertexBudget());
	REQUIRE(scene != nullptr);

	const std::array<uint32_t, 1> big   = { { c_BigMeshlets } };
	const std::array<uint32_t, 1> small = { { c_SmallMeshlets } };

	const assetlib::BMesh bigMesh   = MakeMeshletMesh(big);
	const assetlib::BMesh smallMesh = MakeMeshletMesh(small);

	SECTION("it throws SceneError rather than silently succeeding")
	{
		REQUIRE_THROWS_AS(scene->AddStaticMesh(bigMesh, 0, {}), bgl::SceneError);
	}

	SECTION("the rejected mesh leaves the scene exactly as it found it")
	{
		// The whole point: a mesh the scene refused must cost it nothing. Before the failed add, the
		// small mesh fits; afterwards it must still fit, because the big one gave everything back.
		REQUIRE_THROWS_AS(scene->AddStaticMesh(bigMesh, 0, {}), bgl::SceneError);

		bgl::GeomHandle geom;
		REQUIRE_NOTHROW(geom = scene->AddStaticMesh(smallMesh, 0, {}));
		REQUIRE(scene->IsGeomAlive(geom));

		REQUIRE_NOTHROW(scene->DeleteGeom(geom));
	}

	SECTION("retrying the oversized mesh keeps failing the same way, and costs nothing")
	{
		// This is the editor's actual usage: drop the mesh, get an error, drop it again. Every retry
		// has to be as harmless as the first, or the scene bleeds capacity on every attempt.
		for (int attempt = 0; attempt < 5; ++attempt)
			REQUIRE_THROWS_AS(scene->AddStaticMesh(bigMesh, 0, {}), bgl::SceneError);

		bgl::GeomHandle geom;
		REQUIRE_NOTHROW(geom = scene->AddStaticMesh(smallMesh, 0, {}));
		REQUIRE(scene->IsGeomAlive(geom));
	}

	SECTION("a rejected mesh does not disturb geometry that is already loaded")
	{
		bgl::GeomHandle first;
		REQUIRE_NOTHROW(first = scene->AddStaticMesh(smallMesh, 0, {}));

		REQUIRE_THROWS_AS(scene->AddStaticMesh(bigMesh, 0, {}), bgl::SceneError);

		REQUIRE(scene->IsGeomAlive(first));
		REQUIRE_NOTHROW(scene->DeleteGeom(first));
	}
}

TEST_CASE("The fallback sphere still fits after a mesh was rejected", "[scene][capacity][overflow]")
{
	// The editor's exact sequence: an oversized mesh is dropped onto the preview, the load fails, and
	// the preview falls back to its default sphere -- from inside the handler for that very failure.
	// A rejected mesh that kept what it had allocated would leave no room for the sphere that fit a
	// moment earlier, so the error path would itself throw, out of the catch and into Qt's event loop.
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto desc         = bgl::SceneDesc();
	desc.maxGeom      = 8;
	desc.maxSubmeshes = 64;
	desc.maxMeshlets  = 4000;
	desc.maxVertexBufferByteSize =
		64000;  // the sphere needs ~52 KB of this; the mesh below needs 72
	desc.maxIndices = 20000;

	auto scene = gfx->CreateScene(desc);
	REQUIRE(scene != nullptr);

	// 40 submeshes x 50 meshlets = 72000 bytes of vertex data, over the budget.
	const std::vector<uint32_t> oversized(40, 50);
	const assetlib::BMesh       bigMesh = MakeMeshletMesh(oversized);

	REQUIRE_THROWS_AS(scene->AddStaticMesh(bigMesh, 0, {}), bgl::SceneError);

	bgl::GeomHandle sphere;
	REQUIRE_NOTHROW(sphere = scene->AddSphereGeom(32, 32, 1.0f));
	REQUIRE(scene->IsGeomAlive(sphere));
}

TEST_CASE("A geom rejected at the last hurdle gives back everything", "[scene][capacity][overflow]")
{
	// The budgets are only half the story. A mesh can fit the scene's totals and still not fit what is
	// left of them, in which case it is the allocator that stops it -- part-way through, after some of
	// its arenas have already been written. Here the mesh clears every arena and is turned away at the
	// very last step, the geom slot, so it has the maximum amount to hand back.
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto desc                    = bgl::SceneDesc();
	desc.maxGeom                 = 1;  // the second add gets all the way here, then fails
	desc.maxSubmeshes            = 8;
	desc.maxMeshlets             = 45;
	desc.maxVertexBufferByteSize = 3600;
	desc.maxIndices              = 1000;

	auto scene = gfx->CreateScene(desc);
	REQUIRE(scene != nullptr);

	const std::array<uint32_t, 1> twenty = { { 20 } };
	const std::array<uint32_t, 1> forty  = { { 40 } };

	const assetlib::BMesh mesh   = MakeMeshletMesh(twenty);
	const assetlib::BMesh bigger = MakeMeshletMesh(forty);

	bgl::GeomHandle first;
	REQUIRE_NOTHROW(first = scene->AddStaticMesh(mesh, 0, {}));

	// Fits every budget, so it is not turned away up front -- but there is only one geom slot.
	REQUIRE_THROWS_AS(scene->AddStaticMesh(mesh, 0, {}), bgl::SceneError);

	REQUIRE_NOTHROW(scene->DeleteGeom(first));

	// The scene is empty again, so a mesh that needs most of the meshlet budget must fit. It only does
	// if the rejected add gave back the 20 meshlets it had taken before the geom slot turned it away.
	bgl::GeomHandle second;
	REQUIRE_NOTHROW(second = scene->AddStaticMesh(bigger, 0, {}));
	REQUIRE(scene->IsGeomAlive(second));
}
