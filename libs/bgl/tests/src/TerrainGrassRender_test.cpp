#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/VelocityReadback.h"
#include <assetlib_structs/Heightfield.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/GrassDesc.h>
#include <bgl/types/GrassHandle.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/TerrainDesc.h>
#include <bgl/types/TerrainGrassDesc.h>
#include <bgl/types/TerrainHandle.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

/**
 * Grass on a terrain, proven at the pixel: green blades over a white field, against the same frame
 * with the layer taken away or its rules allowing nowhere. Captures are compared with captures,
 * never with a stored PNG.
 */

namespace
{
	constexpr uint32_t c_Width  = 640;
	constexpr uint32_t c_Height = 480;

	/** A `side` x `side` field a metre a cell, rolling by at most `relief` metres. */
	assetlib::Heightfield
	Rolling(const uint32_t side, const float relief)
	{
		auto field        = assetlib::Heightfield();
		field.samplesX    = side;
		field.samplesZ    = side;
		field.cellSize    = 1.0f;
		field.heightRange = relief;
		field.heights.resize(static_cast<size_t>(side) * side);
		for (uint32_t z = 0; z < side; ++z)
		{
			for (uint32_t x = 0; x < side; ++x)
			{
				const float wave = 0.5f + 0.25f * std::sin(static_cast<float>(x) * 0.3f) +
				                   0.25f * std::cos(static_cast<float>(z) * 0.2f);
				field.heights[z * side + x] = static_cast<uint16_t>(wave * 65535.0f);
			}
		}
		return field;
	}

	/** A `side` x `side` field a metre a cell rising `rise` metres along x, at one slope. */
	assetlib::Heightfield
	Ramp(const uint32_t side, const float rise)
	{
		auto field        = assetlib::Heightfield();
		field.samplesX    = side;
		field.samplesZ    = side;
		field.cellSize    = 1.0f;
		field.heightRange = rise;
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

	struct TerrainGrassScene
	{
		bgl::GraphicsRef      gfx;
		bgl::RenderTargetRef  target;
		bgl::SceneRef         scene;
		bgl::SceneViewRef     view;
		assetlib::Heightfield field;
		bgl::TerrainHandle    terrain;
		bgl::GrassHandle      look;
		bgl::RenderJob        job;

		explicit TerrainGrassScene(assetlib::Heightfield heights) : field(std::move(heights))
		{
			auto opts                        = bgl::test::GraphicsSetup();
			opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
			opts.gpuContext.enableDebugLayer = true;
			gfx                              = bgl::test::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = static_cast<int>(c_Width);
			targetDesc.height   = static_cast<int>(c_Height);
			targetDesc.headless = true;
			target              = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);

			auto sceneDesc                = bgl::SceneDesc();
			sceneDesc.initialPbrMaterials = 4;
			scene                         = gfx->CreateScene(sceneDesc);
			view                          = gfx->CreateSceneView(scene, 8);
			bgl::test::ApplyEnvironment(scene.Get(), view.Get());

			auto whiteDesc            = bgl::PbrMaterialDesc();
			whiteDesc.metallicFactor  = 0.0f;
			whiteDesc.roughnessFactor = 1.0f;
			const auto white          = scene->CreatePbrMaterial(whiteDesc);

			auto greenDesc            = whiteDesc;
			greenDesc.baseColorFactor = glm::vec4(0.1f, 0.8f, 0.1f, 1.0f);
			const auto green          = scene->CreatePbrMaterial(greenDesc);

			terrain =
				scene->CreateTerrain(bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(white));

			auto lookDesc              = bgl::GrassDesc();
			lookDesc.material          = green;
			lookDesc.blade.rootWidth   = 0.05f;
			lookDesc.density.fadeStart = 10.0f;
			lookDesc.density.fadeEnd   = 30.0f;
			look                       = scene->CreateGrass(lookDesc);

			job.view     = view;
			job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));
		}

		/** Three metres over the ground at (x, z), looking at it eight metres ahead along -z. */
		void
		StandAt(const float x, const float z, const float groundY)
		{
			job.camera = bgl::Camera();
			job.camera
				.LookAt(
					glm::vec3(x, groundY + 3.0f, z + 8.0f),
					glm::vec3(x, groundY, z),
					glm::vec3(0.0f, 1.0f, 0.0f))
				.Perspective(
					glm::radians(60.0f),
					static_cast<float>(c_Width) / static_cast<float>(c_Height),
					0.1f,
					300.0f);
		}

		[[nodiscard]] bgl::TerrainGrassDesc
		Layer() const
		{
			return bgl::TerrainGrassDesc().SetLook(look).SetSpacing(0.12f);
		}

		void
		Grow(const bgl::TerrainGrassDesc& layer) const
		{
			scene->AttachTerrainGrass(terrain, std::span<const bgl::TerrainGrassDesc>(&layer, 1));
		}

		void
		Clear() const
		{
			scene->AttachTerrainGrass(terrain, {});
		}

		/** The mean colour of the central box once TAA has blended earlier frames out. */
		[[nodiscard]] bgl::test::Rgba
		Settled(const char* name) const
		{
			for (int i = 0; i < 23; ++i)
			{
				gfx->DrawFrame(target, job);
			}
			const auto path =
				(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();
			gfx->DrawFrame(target, job);
			gfx->ScreenshotPng(target, path);
			const bgl::test::Rgba box = bgl::test::MeanColor(path, 220, 200, 200, 120);
			std::filesystem::remove(path);
			return box;
		}
	};

	[[nodiscard]] float
	Greenness(const bgl::test::Rgba& c)
	{
		return c.g - c.r;
	}

	[[nodiscard]] bool
	Same(const bgl::test::Rgba& a, const bgl::test::Rgba& b)
	{
		constexpr float c_Tolerance = 0.01f;
		return std::abs(a.r - b.r) < c_Tolerance && std::abs(a.g - b.g) < c_Tolerance &&
		       std::abs(a.b - b.b) < c_Tolerance;
	}
}

TEST_CASE("Grass grows on a terrain over the ground it covers", "[terrain][grass][render]")
{
	TerrainGrassScene grass(Rolling(129, 2.0f));
	grass.StandAt(64.0f, 64.0f, 1.0f);

	const bgl::test::Rgba bare = grass.Settled("bernini_terrain_grass_bare");

	grass.Grow(grass.Layer());
	const bgl::test::Rgba grown = grass.Settled("bernini_terrain_grass_grown");

	grass.Clear();
	const bgl::test::Rgba cleared = grass.Settled("bernini_terrain_grass_cleared");

	// The white ground reads grey-white; the green blades pull the box toward green.
	CHECK(Greenness(grown) > Greenness(bare) + 0.1f);
	CHECK(Same(cleared, bare));
}

TEST_CASE("A layer whose rules allow nowhere grows nothing", "[terrain][grass][render]")
{
	TerrainGrassScene grass(Rolling(129, 2.0f));
	grass.StandAt(64.0f, 64.0f, 1.0f);

	const bgl::test::Rgba bare = grass.Settled("bernini_terrain_grass_nowhere_bare");

	SECTION("a height band above the whole field")
	{
		grass.Grow(grass.Layer().SetHeights(10.0f, 20.0f, 0.0f));
		CHECK(Same(grass.Settled("bernini_terrain_grass_above"), bare));
	}

	SECTION("patches covering none of it")
	{
		grass.Grow(grass.Layer().SetPatches(5.0f, 0.0f));
		CHECK(Same(grass.Settled("bernini_terrain_grass_uncovered"), bare));
	}

	SECTION("a height band holding the field does grow")
	{
		grass.Grow(grass.Layer().SetHeights(-1.0f, 3.0f, 0.5f));
		CHECK(Greenness(grass.Settled("bernini_terrain_grass_within")) > Greenness(bare) + 0.1f);
	}
}

TEST_CASE(
	"A slope rule clears grass from ground steeper than it allows",
	"[terrain][grass][render]")
{
	// One slope everywhere: 32 m over 64, about 27 degrees.
	TerrainGrassScene grass(Ramp(65, 32.0f));
	grass.StandAt(32.0f, 32.0f, 16.0f);

	const bgl::test::Rgba bare = grass.Settled("bernini_terrain_grass_ramp_bare");

	grass.Grow(grass.Layer().SetSlope(glm::radians(15.0f), glm::radians(5.0f)));
	const bgl::test::Rgba tooSteep = grass.Settled("bernini_terrain_grass_ramp_steep");

	grass.Grow(grass.Layer().SetSlope(glm::radians(35.0f), 0.0f));
	const bgl::test::Rgba gentle = grass.Settled("bernini_terrain_grass_ramp_gentle");

	CHECK(Same(tooSteep, bare));
	CHECK(Greenness(gentle) > Greenness(bare) + 0.1f);
}

TEST_CASE("Terrain grass grows wherever the camera goes on the field", "[terrain][grass][render]")
{
	// The window of tiles is the camera's, not the field's: a corner far from the origin, beyond
	// any window centred there, grows as the middle does.
	TerrainGrassScene grass(Rolling(257, 2.0f));
	grass.Grow(grass.Layer());

	for (const glm::vec2 at : { glm::vec2(20.0f, 20.0f), glm::vec2(230.0f, 230.0f) })
	{
		grass.StandAt(at.x, at.y, 1.0f);
		const bgl::test::Rgba grown = grass.Settled("bernini_terrain_grass_roaming");

		grass.Clear();
		const bgl::test::Rgba bare = grass.Settled("bernini_terrain_grass_roaming_bare");
		grass.Grow(grass.Layer());

		CHECK(Greenness(grown) > Greenness(bare) + 0.1f);
	}
}

TEST_CASE(
	"Still terrain grass under a still camera writes no motion",
	"[terrain][grass][render][motionvectors]")
{
	TerrainGrassScene grass(Rolling(129, 2.0f));
	grass.StandAt(64.0f, 64.0f, 1.0f);
	grass.Grow(grass.Layer());

	grass.gfx->DrawFrame(grass.target, grass.job);
	grass.gfx->DrawFrame(grass.target, grass.job);

	const std::vector<glm::vec4> motion =
		bgl::test::ReadVelocityTexels(grass.gfx.Get(), grass.target.Get(), c_Width, c_Height);
	for (const glm::vec4& texel : motion)
	{
		REQUIRE(std::abs(texel.x) < 1e-4f);
		REQUIRE(std::abs(texel.y) < 1e-4f);
	}
}
