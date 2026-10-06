#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <assetlib_structs/Heightfield.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/MaterialType.h>
#include <bgl/glm.h>
#include <bgl/idl/Constants.h>
#include <bgl/idl/Terrain.h>
#include <bgl/types/LayerType.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/TerrainDesc.h>
#include <bgl/types/TerrainHandle.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The terrain contract as bgl owns it: what CreateTerrain refuses, the lifetime a terrain has, and
// the record and patch shape the CPU and the stage agree on.

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

	/** A `side` x `side` heightfield rising along x, one metre a cell. */
	assetlib::Heightfield
	Ramp(const uint32_t side = 16)
	{
		auto field        = assetlib::Heightfield();
		field.samplesX    = side;
		field.samplesZ    = side;
		field.cellSize    = 1.0f;
		field.minHeight   = 0.0f;
		field.heightRange = 4.0f;
		field.heights.resize(static_cast<size_t>(side) * side);
		for (uint32_t z = 0; z < side; ++z)
		{
			for (uint32_t x = 0; x < side; ++x)
			{
				field.heights[z * side + x] = static_cast<uint16_t>(x * 65535u / (side - 1));
			}
		}
		return field;
	}

	bgl::TerrainDesc
	Valid(const assetlib::Heightfield& field, const bgl::MaterialHandle material)
	{
		return bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(material);
	}
}

TEST_CASE("a terrain lives until DeleteTerrain", "[terrain][contract]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());

	const assetlib::Heightfield field    = Ramp();
	const bgl::MaterialHandle   material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());

	CHECK_FALSE(scene->IsTerrainAlive(bgl::TerrainHandle()));

	const bgl::TerrainHandle terrain = scene->CreateTerrain(Valid(field, material));
	REQUIRE(terrain.IsValid());
	CHECK(scene->IsTerrainAlive(terrain));

	// A second terrain takes a slot of its own, and the first stays where it was.
	const bgl::TerrainHandle other = scene->CreateTerrain(Valid(field, material));
	REQUIRE(other.IsValid());
	CHECK(other.handle.index != terrain.handle.index);
	CHECK(scene->IsTerrainAlive(terrain));

	scene->DeleteTerrain(terrain);
	CHECK_FALSE(scene->IsTerrainAlive(terrain));
	CHECK(scene->IsTerrainAlive(other));
	CHECK_THROWS_AS(scene->DeleteTerrain(terrain), bgl::SceneError);
	CHECK_THROWS_AS(scene->DeleteTerrain(bgl::TerrainHandle()), bgl::SceneError);

	// The slot is reused, and a handle to its old tenant stays dead.
	const bgl::TerrainHandle reused = scene->CreateTerrain(Valid(field, material));
	CHECK(reused.handle.index == terrain.handle.index);
	CHECK_FALSE(scene->IsTerrainAlive(terrain));
	CHECK(scene->IsTerrainAlive(reused));

	// The material is the caller's: deleting the terrain leaves it usable.
	scene->DeleteTerrain(reused);
	scene->DeleteTerrain(other);
	CHECK_NOTHROW(scene->DeleteTerrain(scene->CreateTerrain(Valid(field, material))));
	CHECK_NOTHROW(scene->DeleteMaterial(material));
}

TEST_CASE("CreateTerrain refuses a terrain no stage could draw", "[terrain][contract]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());

	const bgl::MaterialHandle material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
	const float               nan      = std::numeric_limits<float>::quiet_NaN();

	using Break = std::function<void(assetlib::Heightfield&, bgl::TerrainDesc&)>;
	const std::vector<std::pair<std::string, Break>> breaks = {
		{ "no heightfield",
		  [](assetlib::Heightfield&, bgl::TerrainDesc& d) { d.heightfield = nullptr; } },
		{ "one sample across",
		  [](assetlib::Heightfield& f, bgl::TerrainDesc&) {
			  f.samplesX = 1;
			  f.heights.resize(f.samplesZ);
		  } },
		{ "one sample deep",
		  [](assetlib::Heightfield& f, bgl::TerrainDesc&) {
			  f.samplesZ = 1;
			  f.heights.resize(f.samplesX);
		  } },
		{ "too many samples across",
		  [](assetlib::Heightfield& f, bgl::TerrainDesc&) {
			  f.samplesX = bgl::c_MaxTerrainSamples + 1;
		  } },
		{ "fewer heights than declared",
		  [](assetlib::Heightfield& f, bgl::TerrainDesc&) { f.heights.pop_back(); } },
		{ "more heights than declared",
		  [](assetlib::Heightfield& f, bgl::TerrainDesc&) { f.heights.push_back(0); } },
		{ "a zero cell", [](assetlib::Heightfield& f, bgl::TerrainDesc&) { f.cellSize = 0.0f; } },
		{ "a negative height range",
		  [](assetlib::Heightfield& f, bgl::TerrainDesc&) { f.heightRange             = -1.0f; } },
		{ "a NaN minimum height",
		  [nan](assetlib::Heightfield& f, bgl::TerrainDesc&) { f.minHeight            = nan; } },
		{ "a NaN origin",
		  [nan](assetlib::Heightfield&, bgl::TerrainDesc& d) { d.origin.y             = nan; } },
		{ "no pixels per cell",
		  [](assetlib::Heightfield&, bgl::TerrainDesc& d) { d.pixelsPerCell           = 0.0f; } },
		{ "no material",
		  [](assetlib::Heightfield&, bgl::TerrainDesc& d) { d.material = bgl::MaterialHandle(); } },
		{ "a kNull material",
		  [](assetlib::Heightfield&, bgl::TerrainDesc& d) {
			  d.material.materialType = bgl::MaterialType::kNull;
		  } },
		{ "a masked material",
		  [](assetlib::Heightfield&, bgl::TerrainDesc& d) {
			  d.material.layerType = bgl::LayerType::kMask;
		  } },
		{ "a blended material",
		  [](assetlib::Heightfield&, bgl::TerrainDesc& d) {
			  d.material.layerType = bgl::LayerType::kBlend;
		  } },
	};

	for (const auto& [name, broken] : breaks)
	{
		INFO(name);
		assetlib::Heightfield field = Ramp();
		auto                  desc  = Valid(field, material);
		broken(field, desc);
		CHECK_THROWS_AS(scene->CreateTerrain(desc), bgl::SceneError);
	}

	// Nothing above left a terrain behind: the valid one is the first.
	const assetlib::Heightfield field = Ramp();
	const bgl::TerrainHandle    first = scene->CreateTerrain(Valid(field, material));
	CHECK(first.handle.index == 0);
}

namespace
{
	// A toon character surface as a game writes one, enough to register: its programs read a
	// placement's shading rig off its vertices, which is why a terrain refuses it.
	constexpr std::string_view c_CelCharacter = R"(import bgl.MaterialReader;
import bgl.ToonCharacterSurface;

struct CelParams
{
    [Color]
    [Default(1.0, 1.0, 1.0, 1.0)]
    float4 baseColorFactor;

    ColorSlot baseColor;
};

struct CelCharacter : IToonCharacterSurfaceSource
{
    typealias MaterialParams = CelParams;

    static float Coverage<R : IMaterialReader>(R reader, CelParams params)
    {
        return params.baseColorFactor.a * reader.Sample(params.baseColor, reader.Uv()).a;
    }

    static ToonCharacterSurface Evaluate<R : IMaterialReader>(R reader, CelParams params)
    {
        ToonCharacterSurface surface = ToonCharacterSurface();
        surface.baseColor = params.baseColorFactor * reader.Sample(params.baseColor, reader.Uv());
        return surface;
    }
};
)";

	std::filesystem::path
	CelCharacterDir()
	{
		const std::filesystem::path dir =
			std::filesystem::temp_directory_path() / "bernini_terrain_surfaces";
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
		std::ofstream out(dir / "CelCharacter.slang", std::ios::binary | std::ios::trunc);
		REQUIRE(out.is_open());
		out << c_CelCharacter;
		return dir;
	}
}

TEST_CASE(
	"CreateTerrain refuses a toon character surface, which shades a rig",
	"[terrain][contract]")
{
	auto opts                       = HeadlessOptions();
	opts.gpuContext.clientShaderDir = CelCharacterDir();
	auto gfx                        = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());

	const assetlib::Heightfield field = Ramp();
	const bgl::MaterialHandle   character =
		scene->CreateSurfaceMaterial(bgl::SurfaceMaterialDesc{ .surfaceName = "CelCharacter" });
	CHECK_THROWS_AS(scene->CreateTerrain(Valid(field, character)), bgl::SceneError);

	// Any other kind draws a terrain; a PBR one proves the refusal was the surface's.
	CHECK_NOTHROW(scene->DeleteTerrain(
		scene->CreateTerrain(Valid(field, scene->CreatePbrMaterial(bgl::PbrMaterialDesc())))));
}

TEST_CASE("the terrain record and patch are what both sides compile", "[terrain][contract]")
{
	// Two float4s and four words: the stage reads it as one 48-byte record, on every backend.
	STATIC_CHECK(sizeof(bgl::idl::Terrain) == 48);
	STATIC_CHECK(offsetof(bgl::idl::Terrain, heightRangeAndPixelsPerCell) == 16);
	STATIC_CHECK(offsetof(bgl::idl::Terrain, samplesX) == 32);
	STATIC_CHECK(offsetof(bgl::idl::Terrain, materialOffset) == 44);

	// A patch's grid fills one mesh group's output and no more.
	STATIC_CHECK(
		bgl::idl::cTerrainPatchVertices ==
		(bgl::idl::cTerrainPatchQuads + 1) * (bgl::idl::cTerrainPatchQuads + 1));
	STATIC_CHECK(
		bgl::idl::cTerrainPatchPrims ==
		2 * bgl::idl::cTerrainPatchQuads * bgl::idl::cTerrainPatchQuads);
	STATIC_CHECK(bgl::idl::cTerrainPatchVertices <= bgl::idl::cMaxVerticesPerMeshlet);
	STATIC_CHECK(bgl::idl::cTerrainPatchPrims <= bgl::idl::cMaxPrimsPerMeshlet);

	// The public ceiling and the IDL's are one number, and the level count covers it.
	STATIC_CHECK(bgl::c_MaxTerrainSamples == bgl::idl::cTerrainMaxSamples);
	STATIC_CHECK(
		(static_cast<uint64_t>(bgl::idl::cTerrainPatchQuads)
	     << (bgl::idl::cTerrainMaxLevels - 1)) >= bgl::idl::cTerrainMaxSamples - 1);
}
