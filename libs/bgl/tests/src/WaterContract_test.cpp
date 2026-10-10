#include "util/SkinnedSynth.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/WaterSurface.h"
#include <assetlib_structs/Heightfield.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/MaterialType.h>
#include <bgl/SurfaceType.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/GrassDesc.h>
#include <bgl/types/LayerType.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/TerrainDesc.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdint>
#include <span>
#include <string>

// The water contract as bgl owns it: a water surface registers under its own model, and every door
// that would draw it anywhere but the water phase refuses it.

using namespace bgl;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::MessageMatches;

namespace
{
	bgl::test::GraphicsSetup
	WaterOptions()
	{
		auto opts                        = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer = false;
		opts.gpuContext.clientShaderDir  = bgl::test::WaterSurfaceDir();
		return opts;
	}

	SceneDesc
	WaterScene()
	{
		auto desc                    = SceneDesc();
		desc.initialSurfaceMaterials = 16;
		return desc;
	}

	SurfaceMaterialDesc
	Water(LayerType layer = LayerType::kOpaque)
	{
		return SurfaceMaterialDesc{ .surfaceName = "ProbeWater", .layerType = layer };
	}

	assetlib::Heightfield
	Flat()
	{
		auto field        = assetlib::Heightfield();
		field.samplesX    = 4;
		field.samplesZ    = 4;
		field.cellSize    = 1.0f;
		field.heightRange = 1.0f;
		field.heights.assign(16, uint16_t(0));
		return field;
	}
}

TEST_CASE("A water surface registers under its own model", "[surface][registry][water]")
{
	auto gfx = bgl::test::CreateGraphics(WaterOptions());
	REQUIRE(gfx != nullptr);

	const std::span<const SurfaceType> types = gfx->GetSurfaceTypes();
	REQUIRE(types.size() == 2u);

	CHECK(types[0].surfaceName == "ProbeWater");
	CHECK(types[0].kind == MaterialType::kGameStart);
	CHECK(types[0].shading == SurfaceShading::kWater);
	CHECK(types[1].surfaceName == "Unlit");
	CHECK(types[1].shading == SurfaceShading::kLit);

	REQUIRE(types[0].params.values.size() == 6u);
	CHECK(types[0].params.values[0].name == "deep");
	CHECK(types[0].params.values[0].isColor);
	CHECK(types[0].params.values[5].name == "opacity");
	CHECK(types[0].params.textures.empty());
}

TEST_CASE("A water material is refused a surface on another model", "[surface][water]")
{
	auto gfx = bgl::test::CreateGraphics(WaterOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(WaterScene());

	for (const SurfaceShading other :
	     { SurfaceShading::kPbrSurface, SurfaceShading::kLit, SurfaceShading::kToonCharacter })
	{
		auto expectsOther    = Water();
		expectsOther.shading = other;
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial(expectsOther),
			SceneError,
			MessageMatches(ContainsSubstring(
				"surface 'ProbeWater' shades water (IWaterSurfaceSource), but the material "
				"expects")));
	}

	auto expectsWater    = SurfaceMaterialDesc{ .surfaceName = "Unlit" };
	expectsWater.shading = SurfaceShading::kWater;
	CHECK_THROWS_MATCHES(
		scene->CreateSurfaceMaterial(expectsWater),
		SceneError,
		MessageMatches(ContainsSubstring("expects one that shades water")));

	auto expectsItsOwn    = Water();
	expectsItsOwn.shading = SurfaceShading::kWater;
	CHECK_NOTHROW(scene->CreateSurfaceMaterial(expectsItsOwn));
}

TEST_CASE("A water material takes no alpha layer", "[surface][water]")
{
	auto gfx = bgl::test::CreateGraphics(WaterOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(WaterScene());

	for (const LayerType layer : { LayerType::kMask, LayerType::kBlend, LayerType::kHashed })
	{
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial(Water(layer)),
			SceneError,
			MessageMatches(ContainsSubstring("shades water, which has no alpha layer")));
	}
	CHECK(scene->CreateSurfaceMaterial(Water()).layerType == LayerType::kOpaque);
}

TEST_CASE("A terrain and a grass look refuse a water surface", "[surface][water][terrain][grass]")
{
	auto gfx = bgl::test::CreateGraphics(WaterOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(WaterScene());

	const MaterialHandle water = scene->CreateSurfaceMaterial(Water());

	const assetlib::Heightfield field = Flat();
	CHECK_THROWS_MATCHES(
		scene->CreateTerrain(TerrainDesc().SetHeightfield(&field).SetMaterial(water)),
		SceneError,
		MessageMatches(ContainsSubstring("a water surface is drawn over the ground")));

	auto look     = GrassDesc();
	look.material = water;
	CHECK_THROWS_MATCHES(
		scene->CreateGrass(look),
		SceneError,
		MessageMatches(ContainsSubstring("CreateGrass: a water surface")));

	// A PBR material grows the same look, so the refusal was the surface's.
	look.material = scene->CreatePbrMaterial(PbrMaterialDesc{});
	CHECK_NOTHROW(scene->CreateGrass(look));
}

TEST_CASE("Skinned geometry refuses a water surface", "[surface][water][skinned]")
{
	using bgl::test::skinned_synth::AddSlidingQuadGeom;

	auto gfx = bgl::test::CreateGraphics(WaterOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(WaterScene());

	const MaterialHandle water = scene->CreateSurfaceMaterial(Water());
	CHECK_THROWS_MATCHES(
		(void)AddSlidingQuadGeom(*scene, water),
		SceneError,
		MessageMatches(ContainsSubstring("other than water")));

	const GeomHandle quad = AddSlidingQuadGeom(*scene, scene->CreatePbrMaterial(PbrMaterialDesc{}));
	CHECK_THROWS_MATCHES(
		scene->SetSubmeshMaterial(quad, 0, water),
		SceneError,
		MessageMatches(ContainsSubstring("other than water")));
}
