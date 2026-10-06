#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <array>
#include <assetlib_structs/Heightfield.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/types/GrassDesc.h>
#include <bgl/types/GrassHandle.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/TerrainDesc.h>
#include <bgl/types/TerrainGrassDesc.h>
#include <bgl/types/TerrainHandle.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <vector>

// The terrain grass contract as bgl owns it: what AttachTerrainGrass refuses, and the hold a
// terrain's layers keep on their looks.

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

	assetlib::Heightfield
	Flat(const uint32_t side = 16)
	{
		auto field        = assetlib::Heightfield();
		field.samplesX    = side;
		field.samplesZ    = side;
		field.cellSize    = 1.0f;
		field.heightRange = 1.0f;
		field.heights.assign(static_cast<size_t>(side) * side, 0);
		return field;
	}

	struct GrassOnTerrain
	{
		bgl::GraphicsRef      gfx;
		bgl::SceneRef         scene;
		assetlib::Heightfield field = Flat();
		bgl::MaterialHandle   material;
		bgl::TerrainHandle    terrain;

		GrassOnTerrain()
		{
			gfx = bgl::test::CreateGraphics(HeadlessOptions());
			REQUIRE(gfx != nullptr);
			scene    = gfx->CreateScene(bgl::SceneDesc());
			material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
			terrain  = CreateTerrain();
		}

		[[nodiscard]] bgl::TerrainHandle
		CreateTerrain() const
		{
			return scene->CreateTerrain(
				bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(material));
		}

		[[nodiscard]] bgl::GrassHandle
		CreateLook() const
		{
			auto desc     = bgl::GrassDesc();
			desc.material = material;
			return scene->CreateGrass(desc);
		}

		void
		Attach(const bgl::TerrainHandle on, const bgl::TerrainGrassDesc& layer) const
		{
			scene->AttachTerrainGrass(on, std::span<const bgl::TerrainGrassDesc>(&layer, 1));
		}
	};
}

TEST_CASE(
	"a look a terrain grows cannot be deleted until the terrain lets it go",
	"[terrain][grass][contract]")
{
	const GrassOnTerrain   grass;
	const bgl::GrassHandle look = grass.CreateLook();

	SECTION("attaching no layers lets it go")
	{
		grass.Attach(grass.terrain, bgl::TerrainGrassDesc().SetLook(look));
		CHECK_THROWS_AS(grass.scene->DeleteGrass(look), bgl::SceneError);

		grass.scene->AttachTerrainGrass(grass.terrain, {});
		CHECK_NOTHROW(grass.scene->DeleteGrass(look));
	}

	SECTION("deleting the terrain lets it go")
	{
		const bgl::TerrainHandle other = grass.CreateTerrain();
		grass.Attach(grass.terrain, bgl::TerrainGrassDesc().SetLook(look));
		grass.Attach(other, bgl::TerrainGrassDesc().SetLook(look));

		grass.scene->DeleteTerrain(grass.terrain);
		CHECK_THROWS_AS(grass.scene->DeleteGrass(look), bgl::SceneError);
		grass.scene->DeleteTerrain(other);
		CHECK_NOTHROW(grass.scene->DeleteGrass(look));
	}

	SECTION("attaching again replaces the layers, and the looks they held")
	{
		const bgl::GrassHandle next = grass.CreateLook();
		grass.Attach(grass.terrain, bgl::TerrainGrassDesc().SetLook(look));
		grass.Attach(grass.terrain, bgl::TerrainGrassDesc().SetLook(next));

		CHECK_NOTHROW(grass.scene->DeleteGrass(look));
		CHECK_THROWS_AS(grass.scene->DeleteGrass(next), bgl::SceneError);
	}

	SECTION("one look in two layers is held by both")
	{
		const std::array<bgl::TerrainGrassDesc, 2> layers = {
			bgl::TerrainGrassDesc().SetLook(look),
			bgl::TerrainGrassDesc().SetLook(look).SetSpacing(1.0f),
		};
		grass.scene->AttachTerrainGrass(grass.terrain, layers);
		CHECK_THROWS_AS(grass.scene->DeleteGrass(look), bgl::SceneError);

		grass.scene->AttachTerrainGrass(grass.terrain, {});
		CHECK_NOTHROW(grass.scene->DeleteGrass(look));
	}
}

TEST_CASE(
	"AttachTerrainGrass refuses a layer no rule could read, and changes nothing",
	"[terrain][grass][contract]")
{
	const GrassOnTerrain   grass;
	const bgl::GrassHandle held  = grass.CreateLook();
	const bgl::GrassHandle other = grass.CreateLook();
	grass.Attach(grass.terrain, bgl::TerrainGrassDesc().SetLook(held));

	constexpr float c_Nan = std::numeric_limits<float>::quiet_NaN();
	constexpr float c_Pi  = std::numbers::pi_v<float>;

	const auto valid = bgl::TerrainGrassDesc().SetLook(other);
	struct Refused
	{
		std::string                                      what;
		std::function<void(bgl::TerrainGrassDesc& desc)> spoil;
	};
	const std::vector<Refused> cases = {
		{ "a null look", [](auto& d) { d.look     = bgl::GrassHandle(); } },
		{ "zero spacing", [](auto& d) { d.spacing = 0.0f; } },
		{ "an unbounded spacing",
		  [](auto& d) { d.spacing                 = std::numeric_limits<float>::infinity(); } },
		{ "a spacing too fine for the look's fade", [](auto& d) { d.spacing = 1e-4f; } },
		{ "a slope past vertical", [](auto& d) { d.maxSlope                 = c_Pi * 0.6f; } },
		{ "a negative slope", [](auto& d) { d.maxSlope                      = -0.1f; } },
		{ "a negative slope blend", [](auto& d) { d.slopeBlend              = -0.1f; } },
		{ "heights that cross", [](auto& d) { d.SetHeights(5.0f, 1.0f, 0.0f); } },
		{ "a height that is not a number", [](auto& d) { d.minHeight = c_Nan; } },
		{ "a negative height blend", [](auto& d) { d.heightBlend     = -1.0f; } },
		{ "zero patch size", [](auto& d) { d.patchSize               = 0.0f; } },
		{ "coverage over one", [](auto& d) { d.patchCoverage         = 1.5f; } },
		{ "negative coverage", [](auto& d) { d.patchCoverage         = -0.5f; } },
	};

	for (const Refused& refused : cases)
	{
		INFO(refused.what);
		auto layer = valid;
		refused.spoil(layer);
		CHECK_THROWS_AS(grass.Attach(grass.terrain, layer), bgl::SceneError);
	}

	SECTION("a deleted look")
	{
		const bgl::GrassHandle gone = grass.CreateLook();
		grass.scene->DeleteGrass(gone);
		CHECK_THROWS_AS(
			grass.Attach(grass.terrain, bgl::TerrainGrassDesc().SetLook(gone)),
			bgl::SceneError);
	}

	SECTION("a deleted terrain")
	{
		const bgl::TerrainHandle gone = grass.CreateTerrain();
		grass.scene->DeleteTerrain(gone);
		CHECK_THROWS_AS(grass.Attach(gone, valid), bgl::SceneError);
	}

	SECTION("one bad layer refuses the good one beside it")
	{
		auto bad                                          = valid;
		bad.spacing                                       = -1.0f;
		const std::array<bgl::TerrainGrassDesc, 2> layers = { valid, bad };
		CHECK_THROWS_AS(grass.scene->AttachTerrainGrass(grass.terrain, layers), bgl::SceneError);
	}

	// Every refusal left the terrain growing the look it held, and took no hold on the other.
	CHECK_THROWS_AS(grass.scene->DeleteGrass(held), bgl::SceneError);
	CHECK_NOTHROW(grass.scene->DeleteGrass(other));
}

TEST_CASE("the extreme rules a layer may name are accepted", "[terrain][grass][contract]")
{
	const GrassOnTerrain   grass;
	const bgl::GrassHandle look = grass.CreateLook();

	CHECK_NOTHROW(grass.Attach(
		grass.terrain,
		bgl::TerrainGrassDesc()
			.SetLook(look)
			.SetSlope(0.0f, 10.0f)
			.SetHeights(2.0f, 2.0f, 0.0f)
			.SetPatches(1.0f, 0.0f)));
	CHECK_NOTHROW(grass.Attach(
		grass.terrain,
		bgl::TerrainGrassDesc().SetLook(look).SetSlope(std::numbers::pi_v<float> * 0.5f, 0.0f)));
}
