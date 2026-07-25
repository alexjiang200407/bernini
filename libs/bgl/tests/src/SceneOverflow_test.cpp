#include "scene/Scene.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

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

	// Deliberately far too small for the meshes below: every arena has to grow to take them.
	bgl::SceneDesc
	TightInitialBudget()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 1;
		desc.initialSubmeshes            = 1;
		desc.initialMeshlets             = 1;
		desc.initialVertexBufferByteSize = 64;
		desc.initialIndices              = 1;
		return desc;
	}

	constexpr uint32_t c_BigMeshlets   = 50;  // 1800 bytes of vertex data
	constexpr uint32_t c_SmallMeshlets = 20;  //  720 bytes
}

TEST_CASE("A mesh larger than the scene's initial arenas still loads", "[scene][capacity][growth]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(TightInitialBudget());
	REQUIRE(scene != nullptr);

	const std::array<uint32_t, 1> big   = { { c_BigMeshlets } };
	const std::array<uint32_t, 1> small = { { c_SmallMeshlets } };

	const assetlib::BMesh bigMesh   = MakeMeshletMesh(big);
	const assetlib::BMesh smallMesh = MakeMeshletMesh(small);

	SECTION("the initial sizes are a starting point, not a ceiling")
	{
		bgl::GeomHandle geom;
		REQUIRE_NOTHROW(geom = scene->AddStaticMesh(bigMesh, 0, {}));
		CHECK(scene->IsGeomAlive(geom));
	}

	SECTION("geometry loaded before a growth stays alive and addressable")
	{
		bgl::GeomHandle first;
		REQUIRE_NOTHROW(first = scene->AddStaticMesh(smallMesh, 0, {}));

		// Forces every arena past the capacity `first` was allocated in.
		bgl::GeomHandle second;
		REQUIRE_NOTHROW(second = scene->AddStaticMesh(bigMesh, 0, {}));

		CHECK(scene->IsGeomAlive(first));
		CHECK(scene->IsGeomAlive(second));

		// Growth must not renumber: the ranges `first` recorded before it must still resolve.
		REQUIRE_NOTHROW(scene->DeleteGeom(first));
		REQUIRE_NOTHROW(scene->DeleteGeom(second));
	}

	SECTION("repeated loads past the initial geom table keep working")
	{
		// initialGeom is 1, so every add after the first grows the geom table too.
		std::vector<bgl::GeomHandle> geoms;
		for (int i = 0; i < 8; ++i)
		{
			bgl::GeomHandle geom;
			REQUIRE_NOTHROW(geom = scene->AddStaticMesh(smallMesh, 0, {}));
			geoms.push_back(geom);
		}

		for (const bgl::GeomHandle geom : geoms)
		{
			CHECK(scene->IsGeomAlive(geom));
		}
	}

	SECTION("procedural geometry grows the same arenas")
	{
		bgl::GeomHandle sphere;
		REQUIRE_NOTHROW(sphere = scene->AddSphereGeom(32, 32, 1.0f));
		CHECK(scene->IsGeomAlive(sphere));

		bgl::GeomHandle mesh;
		REQUIRE_NOTHROW(mesh = scene->AddStaticMesh(bigMesh, 0, {}));
		CHECK(scene->IsGeomAlive(mesh));
		CHECK(scene->IsGeomAlive(sphere));
	}
}

TEST_CASE("A mesh past the DispatchMesh group cap is still refused", "[scene][capacity][growth]")
{
	// Growth removed the arena budgets, but this limit is the hardware's: one DispatchMesh can launch
	// at most 65535 groups, so a submesh past that cannot be drawn however much memory there is.
	// Nothing should have quietly turned it into an allocation that succeeds.
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(TightInitialBudget());
	REQUIRE(scene != nullptr);

	const std::array<uint32_t, 1> overCap = { { 70000 } };
	const assetlib::BMesh         mesh    = MakeMeshletMesh(overCap);

	REQUIRE_THROWS_AS(scene->AddStaticMesh(mesh, 0, {}), bgl::SceneError);
}
