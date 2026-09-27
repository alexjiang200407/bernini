#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/TestOptions.h"
#include <array>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LodLevel.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl_common/idl/Geom.h>
#include <bgl_common/idl/LodSubmeshRange.h>
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
	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
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

	/** One submesh entry of the in-memory mesh: its triangle count and its box. */
	struct EntrySpec
	{
		uint32_t  triangles;
		glm::vec3 aabbMin;
		glm::vec3 aabbMax;
	};

	/**
	 * One mesh of `submeshCount` submeshes over `entries.size() / submeshCount` levels, every
	 * triangle its own meshlet, and `lods` as its thresholds (none when empty).
	 */
	assetlib::BMesh
	MakeLodMesh(std::span<const EntrySpec> entries, uint32_t submeshCount, std::vector<float> lods)
	{
		constexpr uint16_t c_Stride = 12;

		auto     mesh          = assetlib::BMesh();
		uint32_t totalVertices = 0;
		for (const EntrySpec& entry : entries) totalVertices += entry.triangles * 3;
		mesh.vertexData.resize(static_cast<size_t>(totalVertices) * c_Stride);

		uint32_t vertexCursor = 0;
		for (const EntrySpec& spec : entries)
		{
			const auto firstMeshlet = static_cast<uint32_t>(mesh.meshlets.size());
			for (uint32_t i = 0; i < spec.triangles; ++i)
			{
				auto meshlet           = assetlib::Meshlet();
				meshlet.vertexOffset   = static_cast<uint32_t>(mesh.meshletVertices.size());
				meshlet.triangleOffset = static_cast<uint32_t>(mesh.meshletTriangles.size());
				meshlet.vertexCount    = 3;
				meshlet.triangleCount  = 1;
				meshlet.boundingRadius = 1.0f;
				mesh.meshlets.push_back(meshlet);
				for (uint32_t v = 0; v < 3; ++v) mesh.meshletVertices.push_back(i * 3 + v);
				for (uint8_t t = 0; t < 3; ++t) mesh.meshletTriangles.push_back(t);
			}

			auto submesh                  = assetlib::Submesh();
			submesh.layout.attributeCount = 1;
			submesh.layout.stride         = c_Stride;
			submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
				                              assetlib::VertexFormat::kFloat32x3,
				                              0 };
			submesh.vertexByteOffset      = vertexCursor * c_Stride;
			submesh.vertexCount           = spec.triangles * 3;
			submesh.firstMeshlet          = firstMeshlet;
			submesh.meshletCount          = spec.triangles;
			submesh.material              = 0;
			submesh.aabbMin               = spec.aabbMin;
			submesh.aabbMax               = spec.aabbMax;
			mesh.submeshes.push_back(submesh);

			vertexCursor += spec.triangles * 3;
		}

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = submeshCount;
		entry.lodCount     = static_cast<uint32_t>(entries.size()) / submeshCount;
		entry.firstLod     = 0;
		mesh.meshes.push_back(entry);

		for (const float minPixels : lods) mesh.lods.push_back({ minPixels });
		return mesh;
	}

	// Two source submeshes over two levels. Level 1's boxes are far larger, so a sphere measured
	// over every level would show it.
	const std::array<EntrySpec, 4> c_TwoLevels = { {
		{ 4, glm::vec3(-1.0f, 0.0f, -1.0f), glm::vec3(1.0f, 2.0f, 1.0f) },
		{ 3, glm::vec3(-1.0f, 2.0f, -1.0f), glm::vec3(1.0f, 4.0f, 1.0f) },
		{ 2, glm::vec3(-50.0f), glm::vec3(50.0f) },
		{ 1, glm::vec3(-50.0f), glm::vec3(50.0f) },
	} };
}

TEST_CASE("every level of a mesh uploads into its geom, level-major", "[lod][geom]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
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
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
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
