#include "util/AgxProbe.h"
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
// Two references, kept apart because they answer different questions.
//
// The level is checked against the exact cosine integral of the source at the two limb normals,
// pushed through the shipped tone map by RunAgX. Blender's Cycles render of the same sphere is
// that integral to within a percent, so this is what "Blender's level" means once Blender's own
// tone map is taken out of the comparison -- which it has to be, because it is not ours (below).
// A 1.34 exposure, the normalization this asset used to ship with, moves these by about 0.06.
//
// The backdrop corners are checked against Blender's frame directly, and loosely: they are foliage
// resampled twice over, and their job is to catch a mirrored or rotated environment, which moves
// them by far more than any level. The two renderers run longitude the opposite way round an
// equirectangular source, so each of Bernini's corners is compared against Blender's opposite one.
//
// What Blender 5.2.1 measured, display luma over the same boxes (blender_probe.py, 64 samples,
// `--engine CYCLES` for the first row and the default Eevee for the second):
//
//   Cycles, Lambert     lit 0.5393   dark 0.3608   the exact integral through Blender's AgX
//   Eevee, Lambert      lit 0.4925   dark 0.3995   Eevee's world diffuse is an L1 harmonic: flatter
//   Blender AgX(0.18)   0.4612       ours 0.5005   Blender pins middle grey where sRGB puts it
//
// The last line is why the level is not asserted against Blender's numbers: the shipped fit sits
// 0.02-0.05 above Blender's AgX through the whole midtone range, and a margin wide enough to hide
// that would hide an exposure too. It is a tone-map term, and it is measured, not ours to absorb.

namespace
{
	constexpr uint32_t c_Width  = 400;
	constexpr uint32_t c_Height = 300;

	constexpr int c_BoxSize = 16;

	// The sphere's limbs, and two backdrop corners. Symmetric about the frame's centre line, so a
	// mirrored frame maps each box onto its opposite.
	constexpr int c_SphereLeftX  = 141;
	constexpr int c_SphereRightX = 243;
	constexpr int c_SphereY      = 142;
	constexpr int c_SkyLeftX     = 10;
	constexpr int c_SkyRightX    = 374;
	constexpr int c_SkyY         = 20;

	// Irradiance of the source at the normal under each limb box, (+-0.632, 0, 0.775): the cosine
	// integral of forest.exr in the map's 1/pi convention, as blender_probe.py prints it under
	// `irradiance`. Bernini's +X is the screen's right.
	constexpr float c_Albedo          = 0.18f;
	constexpr float c_IrradianceRight = 1.4481f;
	constexpr float c_IrradianceLeft  = 0.6377f;

	// Display luma Blender's Cycles rendered over the backdrop corners, its own left and right.
	constexpr float c_BlenderSkyLeft  = 0.7834f;
	constexpr float c_BlenderSkyRight = 0.4690f;

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

	const float expectedLeft =
		bgl::test::EncodeSrgb(bgl::test::RunAgX(*gfx, c_Albedo * c_IrradianceLeft).r);
	const float expectedRight =
		bgl::test::EncodeSrgb(bgl::test::RunAgX(*gfx, c_Albedo * c_IrradianceRight).r);

	INFO(
		"sphere L/R " << sphereLeft.Luma() << "/" << sphereRight.Luma() << " expected "
					  << expectedLeft << "/" << expectedRight);
	INFO(
		"sky L/R " << skyLeft.Luma() << "/" << skyRight.Luma() << " against Blender's R/L "
				   << c_BlenderSkyRight << "/" << c_BlenderSkyLeft);

	CHECK(std::abs(skyLeft.Luma() - c_BlenderSkyRight) < c_SkyMargin);
	CHECK(std::abs(skyRight.Luma() - c_BlenderSkyLeft) < c_SkyMargin);

	CHECK(std::abs(sphereLeft.Luma() - expectedLeft) < c_LevelMargin);
	CHECK(std::abs(sphereRight.Luma() - expectedRight) < c_LevelMargin);
}
