#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/SkyboxDesc.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <string>

// The shipped `forest` environment exists to be compared against Blender's Material Preview, and a
// golden cannot say whether a render sits at the right level -- only that it has not moved. This
// case renders the probe scripts/blender_probe.py renders in Blender: a Lambertian middle-grey
// sphere under Blender's own forest.exr, from the same camera, and asks where it lands on screen.
//
// The level is checked against Blender's Eevee pixels directly: Eevee is the Material Preview, which
// is what the shipped environment exists to be compared against, and `forest` is baked with the
// irradiance model that lights the way Eevee's preview does. Cycles, the exact integral, stays in
// the table below as the answer the model deliberately does not give. A 1.34 exposure, the
// normalization this asset used to ship with, moves these by about 0.06.
//
// The backdrop corners are checked against Blender's frame loosely: they are foliage resampled twice
// over, and their job is to catch a mirrored or rotated environment, which moves them by far more
// than any level. Bernini reads an equirectangular source's longitude the way Blender does, so each
// box is compared with Blender's same side from Blender's own front view.
//
// What Blender 5.2.1 measured, display luma over the same boxes (blender_probe.py, 64 samples,
// `--engine CYCLES` for the first row and the default Eevee for the second):
//
//   Cycles, Lambert     left 0.3973   right 0.5406   the exact integral through Blender's AgX
//   Eevee, Lambert      left 0.4281   right 0.4945   the Material Preview: a first-order harmonic

namespace
{
	constexpr uint32_t c_Width  = 400;
	constexpr uint32_t c_Height = 300;

	constexpr int c_BoxSize = 16;

	// The sphere's limbs, and two backdrop corners, at the pixels blender_probe.py reads.
	constexpr int c_SphereLeftX  = 141;
	constexpr int c_SphereRightX = 243;
	constexpr int c_SphereY      = 142;
	constexpr int c_SkyLeftX     = 10;
	constexpr int c_SkyRightX    = 374;
	constexpr int c_SkyY         = 20;

	constexpr float c_Albedo = 0.18f;

	// Display luma Blender's Eevee rendered over the sphere's limbs, and its Cycles over the
	// backdrop corners, which the engine does not change.
	constexpr float c_BlenderSphereLeft  = 0.4281f;
	constexpr float c_BlenderSphereRight = 0.4945f;
	constexpr float c_BlenderSkyLeft     = 0.5595f;
	constexpr float c_BlenderSkyRight    = 0.2046f;

	constexpr float c_LevelMargin = 0.015f;
	constexpr float c_SkyMargin   = 0.08f;
}

TEST_CASE("A matte sphere under forest sits at Blender's level", "[pbr][ibl][parity][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 512;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 400000;
	sceneDesc.initialIndices              = 20000;
	sceneDesc.initialPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());
	view->SetSkyBox(bgl::SkyboxDesc{ bgl::test::LoadSkybox(scene.Get()) });

	// Lambertian: the two renderers' split-sum speculars differ by construction, and a level is
	// what is being measured.
	const auto matte = scene->CreatePbrMaterial(
		{ .baseColorFactor = glm::vec4(c_Albedo, c_Albedo, c_Albedo, 1.0f),
	      .metallicFactor  = 0.0f,
	      .roughnessFactor = 1.0f,
	      .specularFactor  = 0.0f });

	const auto sphere = scene->AddSphereGeom(64, 32, 5.0f, matte);
	(void)view->CreateStaticMeshInstance(sphere, glm::mat4(1.0f));

	auto camera = bgl::Camera();
	camera.LookAt(glm::vec3(0.0f, 0.0f, 20.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(60.0f),
			static_cast<float>(c_Width) / static_cast<float>(c_Height),
			0.5f,
			500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);

	const std::string shot = "assets/golden/blender_parity.got.png";
	gfx->ScreenshotPng(target, shot);

	const auto sphereLeft =
		bgl::test::MeanColor(shot, c_SphereLeftX, c_SphereY, c_BoxSize, c_BoxSize);
	const auto sphereRight =
		bgl::test::MeanColor(shot, c_SphereRightX, c_SphereY, c_BoxSize, c_BoxSize);
	const auto skyLeft  = bgl::test::MeanColor(shot, c_SkyLeftX, c_SkyY, c_BoxSize, c_BoxSize);
	const auto skyRight = bgl::test::MeanColor(shot, c_SkyRightX, c_SkyY, c_BoxSize, c_BoxSize);

	INFO(
		"sphere L/R " << sphereLeft.Luma() << "/" << sphereRight.Luma() << " against Blender's L/R "
					  << c_BlenderSphereLeft << "/" << c_BlenderSphereRight);
	INFO(
		"sky L/R " << skyLeft.Luma() << "/" << skyRight.Luma() << " against Blender's L/R "
				   << c_BlenderSkyLeft << "/" << c_BlenderSkyRight);

	CHECK(std::abs(skyLeft.Luma() - c_BlenderSkyLeft) < c_SkyMargin);
	CHECK(std::abs(skyRight.Luma() - c_BlenderSkyRight) < c_SkyMargin);

	CHECK(std::abs(sphereLeft.Luma() - c_BlenderSphereLeft) < c_LevelMargin);
	CHECK(std::abs(sphereRight.Luma() - c_BlenderSphereRight) < c_LevelMargin);
}
