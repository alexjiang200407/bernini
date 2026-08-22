#include "Windows/MaterialEditor/mesh_preview.h"

#include "util/MeshFixture.h"

#include <catch2/catch_test_macros.hpp>

// What the material preview's load does before it reaches the render thread. This is the seam the
// panel's freeze was hiding behind: everything here used to run inside the renderer's closure, and
// a regression that put any of it back would show up as one of these assertions moving into a test
// that needs a device.

namespace
{
	using editor::test::AddMeshNode;
	using editor::test::AddTriangleMesh;
}

TEST_CASE("The preview cooks each mesh once and places one instance per node", "[materialeditor]")
{
	auto mesh = assetlib::BMesh();

	const uint32_t two = AddTriangleMesh(mesh, 2);
	const uint32_t one = AddTriangleMesh(mesh, 1);

	// The first mesh instanced twice, the second once -- the case a per-node cook would get wrong.
	AddMeshNode(mesh, two, -2.0f);
	AddMeshNode(mesh, one, 0.0f);
	AddMeshNode(mesh, two, 2.0f);

	const editor::MeshPreviewBuild build = editor::PrepareMeshPreview(mesh, {});

	SECTION("one cook per distinct mesh entry, one placement per node")
	{
		REQUIRE(build.meshes.size() == 2);
		CHECK(build.meshes[0].meshIndex == two);
		CHECK(build.meshes[1].meshIndex == one);

		REQUIRE(build.placements.size() == 3);
		CHECK(build.placements[0].entry == 0);
		CHECK(build.placements[1].entry == 1);
		CHECK(build.placements[2].entry == 0);  // the second node of the twice-instanced mesh

		// Each node's own transform, not the first one's applied three times.
		CHECK(build.placements[0].world[3].x == -2.0f);
		CHECK(build.placements[1].world[3].x == 0.0f);
		CHECK(build.placements[2].world[3].x == 2.0f);
	}

	SECTION("the submesh table is per mesh entry, not per placement")
	{
		// Three: two from the first entry and one from the second. A table built per node would
		// hold five, and the selector would list the twice-placed mesh's submeshes twice.
		REQUIRE(build.submeshRefs.size() == 3);
		CHECK(build.submeshNames.size() == 3);
		CHECK(build.submeshMaterialPaths.size() == 3);

		CHECK(build.submeshRefs[0].entry == 0);
		CHECK(build.submeshRefs[0].localSubmesh == 0);
		CHECK(build.submeshRefs[1].entry == 0);
		CHECK(build.submeshRefs[1].localSubmesh == 1);
		CHECK(build.submeshRefs[2].entry == 1);
		CHECK(build.submeshRefs[2].localSubmesh == 0);

		// The source index the material editor rewrites the .bmesh by; distinct across entries.
		CHECK(build.submeshRefs[0].sourceSubmesh == 0);
		CHECK(build.submeshRefs[1].sourceSubmesh == 1);
		CHECK(build.submeshRefs[2].sourceSubmesh == 2);
	}

	SECTION("the picking copy is built here, and already answers a ray")
	{
		// The build carries a raycaster that has never been near the render thread. A ray down -Z
		// through the third node's triangle must hit the geometry the first entry registered.
		const auto ray = game::Ray{ glm::vec3(2.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f) };

		const std::optional<game::Raycaster::Hit> hit = build.raycaster.Raycast(ray);
		REQUIRE(hit.has_value());
		CHECK(hit->instance == 2);
	}
}

TEST_CASE("A mesh no node references cannot be previewed", "[materialeditor]")
{
	auto mesh = assetlib::BMesh();
	(void)AddTriangleMesh(mesh, 1);

	// The refusal has to happen here rather than in the commit: reaching the render thread with
	// nothing to upload is how the panel used to stall and then show an empty viewport.
	CHECK_THROWS_AS(editor::PrepareMeshPreview(mesh, {}), std::runtime_error);
}
