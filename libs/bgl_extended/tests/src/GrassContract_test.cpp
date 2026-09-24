#include "util/TestOptions.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/GeomHandle.h>
#include <bgl/GrassHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/glm.h>
#include <bgl/types/GrassDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/WindDesc.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// The grass contract as bgl owns it: what CreateGrass and SetWind refuse, the lifetime a bound look
// has, and the grass ranges CookStaticMesh checks before it reads them.

namespace
{
	constexpr uint32_t c_GrassSlot = 1;

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
		return opts;
	}

	/**
	 * One triangle in slot 0 and one grass field of `clumps` clumps in slot 1, chunked the way the
	 * cook chunks: full runs of c_GrassClumpsPerChunk and a short last one.
	 */
	assetlib::BMesh
	MakeGrassMesh(const uint32_t clumps = 100)
	{
		constexpr uint16_t c_Stride = 12;

		auto mesh = assetlib::BMesh();

		const std::array<glm::vec3, 3> positions = { glm::vec3(-1.0f, 0.0f, -1.0f),
			                                         glm::vec3(1.0f, 0.0f, -1.0f),
			                                         glm::vec3(0.0f, 0.0f, 1.0f) };
		mesh.vertexData.resize(positions.size() * c_Stride);
		std::memcpy(mesh.vertexData.data(), positions.data(), mesh.vertexData.size());

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexCount    = 3;
		meshlet.triangleCount  = 1;
		meshlet.boundingRadius = 2.0f;
		mesh.meshlets.emplace_back(meshlet);
		for (const uint32_t v : { 0u, 1u, 2u })
		{
			mesh.meshletVertices.emplace_back(v);
			mesh.meshletTriangles.emplace_back(static_cast<uint8_t>(v));
		}

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 1;
		submesh.layout.stride         = c_Stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.vertexCount           = 3;
		submesh.meshletCount          = 1;
		submesh.material              = 0;
		submesh.aabbMin               = glm::vec3(-1.0f, 0.0f, -1.0f);
		submesh.aabbMax               = glm::vec3(1.0f, 0.0f, 1.0f);
		mesh.submeshes.emplace_back(submesh);

		mesh.meshes.emplace_back(assetlib::Mesh{ .firstSubmesh = 0, .submeshCount = 1 });
		mesh.materials = { "Materials/ground.bmaterial", "Grass/verge.bgrass" };

		for (uint32_t c = 0; c < clumps; ++c)
		{
			mesh.grass.clumps.emplace_back(
				assetlib::GrassClump{ .position    = glm::vec3(static_cast<float>(c) * 0.1f, 0, 0),
			                          .heightScale = 1.0f,
			                          .normal      = glm::vec3(0.0f, 1.0f, 0.0f),
			                          .color       = glm::u8vec4(255) });
		}

		auto field       = assetlib::GrassField();
		field.material   = c_GrassSlot;
		field.firstChunk = 0;
		for (uint32_t first = 0; first < clumps; first += assetlib::c_GrassClumpsPerChunk)
		{
			const uint32_t count = std::min(assetlib::c_GrassClumpsPerChunk, clumps - first);
			mesh.grass.chunks.emplace_back(
				assetlib::GrassChunk{ .boundingCenter = glm::vec3(0.0f),
			                          .boundingRadius = 10.0f,
			                          .firstClump     = first,
			                          .clumpCount     = count,
			                          .maxHeightScale = 1.0f });
			++field.chunkCount;
		}
		mesh.grass.fields.emplace_back(field);

		return mesh;
	}

	bgl::GrassDesc
	ValidLook(const bgl::MaterialHandle material)
	{
		auto desc     = bgl::GrassDesc();
		desc.material = material;
		return desc;
	}
}

TEST_CASE("a grass look takes updates until DeleteGrass", "[grass][contract]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());

	auto                   desc  = ValidLook(scene->CreatePbrMaterial(bgl::PbrMaterialDesc()));
	const bgl::GrassHandle grass = scene->CreateGrass(desc);
	REQUIRE(grass.IsValid());

	desc.blade.maxHeight = 0.7f;
	CHECK_NOTHROW(scene->UpdateGrass(grass, desc));

	scene->DeleteGrass(grass);
	CHECK_THROWS_AS(scene->UpdateGrass(grass, desc), bgl::SceneError);
	CHECK_THROWS_AS(scene->DeleteGrass(grass), bgl::SceneError);
	CHECK_THROWS_AS(scene->DeleteGrass(bgl::GrassHandle()), bgl::SceneError);
}

TEST_CASE("CreateGrass refuses a look no pass could draw", "[grass][contract]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());

	const bgl::MaterialHandle material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
	const float               nan      = std::numeric_limits<float>::quiet_NaN();

	using Break                                             = std::function<void(bgl::GrassDesc&)>;
	const std::vector<std::pair<std::string, Break>> breaks = {
		{ "no material", [](bgl::GrassDesc& d) { d.material = bgl::MaterialHandle(); } },
		{ "a kNull material",
		  [](bgl::GrassDesc& d) { d.material.materialType   = bgl::MaterialType::kNull; } },
		{ "a blended material",
		  [](bgl::GrassDesc& d) { d.material.layerType      = bgl::LayerType::kBlend; } },
		{ "a zero height", [](bgl::GrassDesc& d) { d.blade.minHeight = 0.0f; } },
		{ "min above max",
		  [](bgl::GrassDesc& d) { d.blade.minHeight = 2.0f * d.blade.maxHeight; } },
		{ "a NaN width", [nan](bgl::GrassDesc& d) { d.blade.rootWidth           = nan; } },
		{ "a tip wider than the root", [](bgl::GrassDesc& d) { d.blade.tipWidth = 1.5f; } },
		{ "a negative lean", [](bgl::GrassDesc& d) { d.blade.lean               = -0.1f; } },
		{ "no far segments", [](bgl::GrassDesc& d) { d.blade.farSegments        = 0; } },
		{ "more far segments than near",
		  [](bgl::GrassDesc& d) { d.blade.farSegments  = d.blade.nearSegments + 1; } },
		{ "too many segments",
		  [](bgl::GrassDesc& d) { d.blade.nearSegments = bgl::c_MaxGrassBladeSegments + 1; } },
		{ "no blades", [](bgl::GrassDesc& d) { d.clump.bladesPerClump = 0; } },
		{ "too many blades",
		  [](bgl::GrassDesc& d) { d.clump.bladesPerClump = bgl::c_MaxGrassBladesPerClump + 1; } },
		{ "a negative clump radius", [](bgl::GrassDesc& d) { d.clump.radius = -1.0f; } },
		{ "a fade that ends before it starts",
		  [](bgl::GrassDesc& d) { d.density.fadeEnd = d.density.fadeStart; } },
		{ "negative widening", [](bgl::GrassDesc& d) { d.density.widening              = -1.0f; } },
		{ "a stiffness above one", [](bgl::GrassDesc& d) { d.response.stiffness        = 1.5f; } },
		{ "a negative gust response", [](bgl::GrassDesc& d) { d.response.gustResponse  = -1.0f; } },
		{ "root occlusion above one", [](bgl::GrassDesc& d) { d.lighting.rootOcclusion = 2.0f; } },
		{ "a far ground-normal blend below zero",
		  [](bgl::GrassDesc& d) { d.lighting.groundNormalFar                           = -0.5f; } },
		{ "negative translucency", [](bgl::GrassDesc& d) { d.lighting.translucency     = -1.0f; } },
		{ "a NaN translucency colour",
		  [nan](bgl::GrassDesc& d) { d.lighting.translucencyColor.y                    = nan; } },
		{ "a negative tint", [](bgl::GrassDesc& d) { d.color.tipTint.x                 = -1.0f; } },
		{ "variation above one", [](bgl::GrassDesc& d) { d.color.variation             = 1.5f; } },
	};

	CHECK_NOTHROW(scene->DeleteGrass(scene->CreateGrass(ValidLook(material))));

	const bgl::GrassHandle live = scene->CreateGrass(ValidLook(material));
	for (const auto& [name, broken] : breaks)
	{
		INFO(name);
		auto desc = ValidLook(material);
		broken(desc);
		CHECK_THROWS_AS(scene->CreateGrass(desc), bgl::SceneError);
		CHECK_THROWS_AS(scene->UpdateGrass(live, desc), bgl::SceneError);
	}
}

TEST_CASE("a look bound by a live geom cannot be deleted", "[grass][contract]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());

	const bgl::MaterialHandle ground = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
	const bgl::GrassHandle    grass  = scene->CreateGrass(ValidLook(ground));

	const std::array<bgl::MaterialHandle, 2> materials = { ground, bgl::MaterialHandle() };
	const std::array<bgl::GrassHandle, 2>    looks     = { bgl::GrassHandle(), grass };

	const bgl::GeomHandle first = scene->AddStaticMeshGeom(MakeGrassMesh(), 0, materials, looks);
	const bgl::GeomHandle second =
		scene->AddStaticMeshGeom(bgl::CookStaticMesh(MakeGrassMesh(), 0), materials, looks);

	CHECK_THROWS_AS(scene->DeleteGrass(grass), bgl::SceneError);
	scene->DeleteGeom(first);
	CHECK_THROWS_AS(scene->DeleteGrass(grass), bgl::SceneError);
	scene->DeleteGeom(second);
	CHECK_NOTHROW(scene->DeleteGrass(grass));
}

TEST_CASE("a grass field bound to no look holds nothing", "[grass][contract]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());

	const bgl::MaterialHandle ground = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
	const bgl::GrassHandle    grass  = scene->CreateGrass(ValidLook(ground));
	const std::array<bgl::MaterialHandle, 1> materials = { ground };

	SECTION("the grass span is shorter than the field's slot")
	{
		const std::array<bgl::GrassHandle, 1> looks = { grass };
		const bgl::GeomHandle geom = scene->AddStaticMeshGeom(MakeGrassMesh(), 0, materials, looks);
		CHECK_NOTHROW(scene->DeleteGrass(grass));
		scene->DeleteGeom(geom);
	}

	SECTION("the slot holds a null handle")
	{
		const std::array<bgl::GrassHandle, 2> looks = { grass, bgl::GrassHandle() };
		const bgl::GeomHandle geom = scene->AddStaticMeshGeom(MakeGrassMesh(), 0, materials, looks);
		CHECK_NOTHROW(scene->DeleteGrass(grass));
		scene->DeleteGeom(geom);
	}

	SECTION("no grass span at all, as every caller before grass passes")
	{
		const bgl::GeomHandle geom = scene->AddStaticMeshGeom(MakeGrassMesh(), 0, materials);
		CHECK_NOTHROW(scene->DeleteGrass(grass));
		scene->DeleteGeom(geom);
	}
}

TEST_CASE("AddStaticMeshGeom refuses a deleted look", "[grass][contract]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());

	const bgl::MaterialHandle ground = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
	const bgl::GrassHandle    grass  = scene->CreateGrass(ValidLook(ground));
	scene->DeleteGrass(grass);

	const std::array<bgl::MaterialHandle, 1> materials = { ground };
	const std::array<bgl::GrassHandle, 2>    looks     = { bgl::GrassHandle(), grass };
	CHECK_THROWS_AS(
		scene->AddStaticMeshGeom(MakeGrassMesh(), 0, materials, looks),
		bgl::SceneError);
}

TEST_CASE("CookStaticMesh refuses grass ranges the file cannot back", "[grass][contract]")
{
	SECTION("a well-formed field, and one belonging to another mesh, cook")
	{
		auto mesh = MakeGrassMesh();
		CHECK_NOTHROW(bgl::CookStaticMesh(mesh, 0));

		mesh.grass.fields.front().mesh       = 1;
		mesh.grass.fields.front().firstChunk = 1000;
		CHECK_NOTHROW(bgl::CookStaticMesh(mesh, 0));
	}

	SECTION("a field with no chunks")
	{
		auto mesh                            = MakeGrassMesh();
		mesh.grass.fields.front().chunkCount = 0;
		CHECK_THROWS_AS(bgl::CookStaticMesh(mesh, 0), bgl::SceneError);
	}

	SECTION("chunks past the end of the pool")
	{
		auto mesh                            = MakeGrassMesh();
		mesh.grass.fields.front().firstChunk = 1;
		CHECK_THROWS_AS(bgl::CookStaticMesh(mesh, 0), bgl::SceneError);
	}

	SECTION("a chunk with no clumps")
	{
		auto mesh                           = MakeGrassMesh();
		mesh.grass.chunks.back().clumpCount = 0;
		CHECK_THROWS_AS(bgl::CookStaticMesh(mesh, 0), bgl::SceneError);
	}

	SECTION("a chunk larger than the cook's chunk size")
	{
		auto mesh                            = MakeGrassMesh(2 * assetlib::c_GrassClumpsPerChunk);
		mesh.grass.chunks.front().clumpCount = assetlib::c_GrassClumpsPerChunk + 1;
		CHECK_THROWS_AS(bgl::CookStaticMesh(mesh, 0), bgl::SceneError);
	}

	SECTION("clumps past the end of the pool")
	{
		auto mesh = MakeGrassMesh();
		mesh.grass.clumps.pop_back();
		CHECK_THROWS_AS(bgl::CookStaticMesh(mesh, 0), bgl::SceneError);
	}
}

TEST_CASE("SetWind refuses a wind no pass could evaluate", "[grass][wind]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 1);

	auto wind         = bgl::WindDesc();
	wind.direction    = glm::vec3(0.0f, 0.0f, 2.0f);
	wind.strength     = 0.3f;
	wind.gustStrength = 0.2f;
	CHECK_NOTHROW(view->SetWind(wind));

	const float nan    = std::numeric_limits<float>::quiet_NaN();
	using Break        = std::function<void(bgl::WindDesc&)>;
	const auto refused = std::vector<std::pair<std::string, Break>>{
		{ "a NaN direction", [nan](bgl::WindDesc& w) { w.direction.x = nan; } },
		{ "a straight-up direction",
		  [](bgl::WindDesc& w) { w.direction                     = glm::vec3(0.0f, 1.0f, 0.0f); } },
		{ "negative strength", [](bgl::WindDesc& w) { w.strength = -0.1f; } },
		{ "negative gusts", [](bgl::WindDesc& w) { w.gustStrength   = -0.1f; } },
		{ "negative gust speed", [](bgl::WindDesc& w) { w.gustSpeed = -1.0f; } },
		{ "a zero gust scale", [](bgl::WindDesc& w) { w.gustScale   = 0.0f; } },
		{ "an infinite gust scale",
		  [](bgl::WindDesc& w) { w.gustScale = std::numeric_limits<float>::infinity(); } },
	};

	for (const auto& [name, broken] : refused)
	{
		INFO(name);
		auto desc = wind;
		broken(desc);
		CHECK_THROWS_AS(view->SetWind(desc), bgl::SceneError);
	}
}
