#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/LodMesh.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <array>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LodLevel.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/idl/Geom.h>
#include <bgl/idl/LodSubmeshRange.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// A mesh with levels of detail, uploaded: every level's submeshes in the geom's range, level-major;
// the geom's sphere over level 0 and its threshold table; a placement holding one SubmeshInstance per
// source submesh whatever the level count; and everything freed by DeleteGeom. What the cull then
// chooses from the record is the cull's test.

namespace
{
	bgl::test::GraphicsSetup
	HeadlessOptions()
	{
		auto opts                        = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer = false;
		return opts;
	}

	bgl::SceneDesc
	LodSceneDesc()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 4;
		desc.initialSubmeshes            = 16;
		desc.initialMeshlets             = 64;
		desc.initialVertexBufferByteSize = 16000;
		desc.initialIndices              = 1000;
		desc.initialPbrMaterials         = 4;
		return desc;
	}

	// Two source submeshes over two levels. Level 1's boxes are far larger, so a sphere measured
	// over every level would show it.
	using bgl::test::EntrySpec;
	using bgl::test::MakeLodMesh;

	const std::array<EntrySpec, 4> c_TwoLevels = { {
		{ 4, glm::vec3(-1.0f, 0.0f, -1.0f), glm::vec3(1.0f, 2.0f, 1.0f) },
		{ 3, glm::vec3(-1.0f, 2.0f, -1.0f), glm::vec3(1.0f, 4.0f, 1.0f) },
		{ 2, glm::vec3(-50.0f), glm::vec3(50.0f) },
		{ 1, glm::vec3(-50.0f), glm::vec3(50.0f) },
	} };
}

TEST_CASE("every level of a mesh uploads into its geom, level-major", "[lod][geom]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(LodSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const auto material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
	const auto geom     = scene->AddStaticMeshGeom(
		MakeLodMesh(c_TwoLevels, 2, { 120.0f, 0.0f }),
		0,
		std::array{ material });
	REQUIRE(geom.IsValid());

	const bgl::idl::LodSubmeshRange& onCpu = scene->GetGeomSubmeshes(geom.handle.index);
	const bgl::idl::Geom& onGpu = scene->GetGeomBuffer()[scene->GetGeomEntry(geom.handle.index)];

	CHECK(onCpu.submeshCount == 2u);
	CHECK(onCpu.lodCount == 2u);
	CHECK(onGpu.submeshes.submeshCount == 2u);
	CHECK(onGpu.submeshes.lodCount == 2u);
	CHECK(onGpu.submeshes.range.offsetStart == onCpu.range.offsetStart);

	SECTION("each entry holds its level's geometry")
	{
		auto&          submeshes = scene->GetSubmeshBuffer();
		const uint32_t root      = onCpu.range.offsetStart;
		for (uint32_t entry = 0; entry < c_TwoLevels.size(); ++entry)
		{
			INFO("entry " << entry);
			CHECK(submeshes.AtIndex(root + entry).meshlets.count == c_TwoLevels[entry].triangles);
		}
	}

	SECTION("the sphere encloses level 0 and nothing more")
	{
		// Level 0's boxes span -1..1 x 0..4 x -1..1: centre (0, 2, 0), half-diagonal sqrt(6).
		CHECK(onGpu.boundingSphere.x == Catch::Approx(0.0f));
		CHECK(onGpu.boundingSphere.y == Catch::Approx(2.0f));
		CHECK(onGpu.boundingSphere.z == Catch::Approx(0.0f));
		CHECK(onGpu.boundingSphere.w == Catch::Approx(2.449f).margin(1e-3));
	}

	SECTION("the thresholds, zero past the levels")
	{
		CHECK(onGpu.lodMinPixels[0] == 120.0f);
		CHECK(onGpu.lodMinPixels[1] == 0.0f);
		for (uint32_t level = 2; level < bgl::cMaxMeshLods; ++level)
			CHECK(onGpu.lodMinPixels[level] == 0.0f);
	}

	SECTION("a placement holds one instance per source submesh")
	{
		auto view = gfx->CreateSceneView(sceneHandle, 8);
		view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
		CHECK(view->GetInstanceCount() == 2u);
	}

	SECTION("a material is set by source submesh, and a level's entry is not one")
	{
		CHECK_NOTHROW(scene->SetSubmeshMaterial(geom, 1, material));
		CHECK_THROWS_AS(scene->SetSubmeshMaterial(geom, 2, material), bgl::SceneError);
	}

	SECTION("deleting the geom frees every level's ranges")
	{
		auto&          submeshes = scene->GetSubmeshBuffer();
		auto&          meshlets  = scene->GetMeshletBuffer();
		const uint32_t root      = onCpu.range.offsetStart;
		const uint32_t lastLevel = submeshes.AtIndex(root + 3).meshlets.range.offsetStart;
		REQUIRE(meshlets.IsIndexValid(lastLevel));

		scene->DeleteGeom(geom);

		CHECK_FALSE(submeshes.IsIndexValid(root));
		CHECK_FALSE(meshlets.IsIndexValid(lastLevel));
	}
}

TEST_CASE("a mesh without levels uploads as one, never dropped", "[lod][geom]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto  sceneHandle = gfx->CreateScene(LodSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();

	const std::array<EntrySpec, 1> single = { { c_TwoLevels[0] } };
	const auto geom = scene->AddStaticMeshGeom(MakeLodMesh(single, 1, {}), 0, {});

	const bgl::idl::Geom& onGpu = scene->GetGeomBuffer()[scene->GetGeomEntry(geom.handle.index)];
	CHECK(onGpu.submeshes.lodCount == 1u);
	CHECK(onGpu.submeshes.submeshCount == 1u);
	CHECK(onGpu.lodMinPixels[0] == 0.0f);
	CHECK(onGpu.boundingSphere.w > 0.0f);
}

TEST_CASE("a mesh whose levels its file cannot back is refused", "[lod][geom]")
{
	using Catch::Matchers::ContainsSubstring;

	SECTION("more levels than a mesh may carry")
	{
		auto mesh               = MakeLodMesh(c_TwoLevels, 2, { 120.0f, 0.0f });
		mesh.meshes[0].lodCount = bgl::cMaxMeshLods + 1;
		CHECK_THROWS_WITH(bgl::CookStaticMesh(mesh, 0), ContainsSubstring("a mesh carries 1 to"));
	}
	SECTION("no level at all")
	{
		auto mesh               = MakeLodMesh(c_TwoLevels, 2, { 120.0f, 0.0f });
		mesh.meshes[0].lodCount = 0;
		CHECK_THROWS_WITH(bgl::CookStaticMesh(mesh, 0), ContainsSubstring("a mesh carries 1 to"));
	}
	SECTION("levels past the end of the submeshes")
	{
		auto mesh               = MakeLodMesh(c_TwoLevels, 2, { 120.0f, 60.0f, 0.0f });
		mesh.meshes[0].lodCount = 3;
		CHECK_THROWS_WITH(bgl::CookStaticMesh(mesh, 0), ContainsSubstring("past the end"));
	}
	SECTION("levels past the end of the thresholds")
	{
		auto mesh               = MakeLodMesh(c_TwoLevels, 2, { 120.0f, 0.0f });
		mesh.meshes[0].firstLod = 1;
		CHECK_THROWS_WITH(bgl::CookStaticMesh(mesh, 0), ContainsSubstring("thresholds"));
	}
	SECTION("levels with no thresholds to choose them by")
	{
		const auto mesh = MakeLodMesh(c_TwoLevels, 2, {});
		CHECK_THROWS_WITH(bgl::CookStaticMesh(mesh, 0), ContainsSubstring("no thresholds"));
	}
}
