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
#include <bgl/types/GeomHandle.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/TerrainDesc.h>
#include <bgl/types/TerrainHandle.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/**
 * The terrain pass, proven at the pixel: a green field under the camera against the same frame
 * with no terrain. Captures are compared with captures, never with a stored PNG.
 */

namespace
{
	constexpr uint32_t c_Width  = 640;
	constexpr uint32_t c_Height = 480;

	/** A `side` x `side` field a metre a cell, rolling gently so the surface shades. */
	assetlib::Heightfield
	Rolling(const uint32_t side = 65)
	{
		auto field        = assetlib::Heightfield();
		field.samplesX    = side;
		field.samplesZ    = side;
		field.cellSize    = 1.0f;
		field.minHeight   = 0.0f;
		field.heightRange = 3.0f;
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

	struct TerrainScene
	{
		bgl::GraphicsRef      gfx;
		bgl::RenderTargetRef  target;
		bgl::SceneRef         scene;
		bgl::SceneViewRef     view;
		bgl::MaterialHandle   green;
		bgl::GeomHandle       marker;
		assetlib::Heightfield field = Rolling();
		bgl::RenderJob        job;

		TerrainScene()
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

			auto greenDesc            = bgl::PbrMaterialDesc();
			greenDesc.metallicFactor  = 0.0f;
			greenDesc.roughnessFactor = 1.0f;
			greenDesc.baseColorFactor = glm::vec4(0.1f, 0.8f, 0.1f, 1.0f);
			green                     = scene->CreatePbrMaterial(greenDesc);

			// A white marker at the field's far corner, so a frame proves the lighting path.
			auto whiteDesc            = bgl::PbrMaterialDesc();
			whiteDesc.metallicFactor  = 0.0f;
			whiteDesc.roughnessFactor = 1.0f;
			const auto white          = scene->CreatePbrMaterial(whiteDesc);
			marker                    = scene->AddSphereGeom(16, 16, 2.0f, white);
			view->CreateStaticMeshInstance(
				bgl::StaticMeshInstanceDesc().SetGeom(marker).SetTransform(
					glm::translate(glm::mat4(1.0f), glm::vec3(8.0f, 4.0f, 20.0f))));

			// Over the field's middle, looking down and ahead across it.
			auto camera = bgl::Camera();
			camera
				.LookAt(
					glm::vec3(32.0f, 12.0f, 60.0f),
					glm::vec3(32.0f, 0.0f, 32.0f),
					glm::vec3(0.0f, 1.0f, 0.0f))
				.Perspective(
					glm::radians(60.0f),
					static_cast<float>(c_Width) / static_cast<float>(c_Height),
					0.1f,
					500.0f);

			job.view     = view;
			job.camera   = camera;
			job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));
		}

		[[nodiscard]] bgl::TerrainHandle
		Create() const
		{
			return scene->CreateTerrain(
				bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(green));
		}

		/** The mean colour of the frame's central box, rendered now. */
		[[nodiscard]] bgl::test::Rgba
		Centre(const char* name) const
		{
			const auto path =
				(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();
			gfx->DrawFrame(target, job);
			gfx->ScreenshotPng(target, path);
			const bgl::test::Rgba box = bgl::test::MeanColor(path, 220, 200, 200, 120);
			std::filesystem::remove(path);
			return box;
		}
	};
}

TEST_CASE("A terrain draws its field under the camera", "[terrain][render]")
{
	const TerrainScene terrain;

	const bgl::test::Rgba bare = terrain.Centre("bernini_terrain_bare");

	const bgl::TerrainHandle field = terrain.Create();
	const bgl::test::Rgba    grown = terrain.Centre("bernini_terrain_grown");

	terrain.scene->DeleteTerrain(field);
	const bgl::test::Rgba again = terrain.Centre("bernini_terrain_deleted");

	// The sky reads blue-grey; the green field pulls the box toward green.
	CHECK(grown.g - grown.r > bare.g - bare.r + 0.1f);
	CHECK(again.g - again.r < grown.g - grown.r);
}

TEST_CASE(
	"A still terrain under a still camera writes no motion",
	"[terrain][render][motionvectors]")
{
	const TerrainScene terrain;
	(void)terrain.Create();

	terrain.gfx->DrawFrame(terrain.target, terrain.job);
	terrain.gfx->DrawFrame(terrain.target, terrain.job);

	const std::vector<glm::vec4> motion =
		bgl::test::ReadVelocityTexels(terrain.gfx.Get(), terrain.target.Get(), c_Width, c_Height);
	for (const glm::vec4& texel : motion)
	{
		REQUIRE(std::abs(texel.x) < 1e-4f);
		REQUIRE(std::abs(texel.y) < 1e-4f);
	}
}

TEST_CASE("A second view of the scene draws the terrain too", "[terrain][render]")
{
	// A terrain is the scene's, not a view's: a view made after it sees it without being told --
	// and with no placement of its own, which is what an empty battlefield is.
	const TerrainScene terrain;
	(void)terrain.Create();

	auto other = terrain.gfx->CreateSceneView(terrain.scene, 8);
	bgl::test::ApplyEnvironment(terrain.scene.Get(), other.Get());
	auto job = terrain.job;
	job.view = other;
	const auto path =
		(std::filesystem::temp_directory_path() / "bernini_terrain_other_view.png").string();
	terrain.gfx->DrawFrame(terrain.target, job);
	terrain.gfx->ScreenshotPng(terrain.target, path);
	const bgl::test::Rgba box = bgl::test::MeanColor(path, 220, 200, 200, 120);
	std::filesystem::remove(path);

	CHECK(box.g > box.r + 0.1f);
}
