#include "util/AgxProbe.h"
#include "util/GoldenImage.h"
#include "util/SyntheticCube.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/SkyboxDesc.h>
#include <bgl/types/DirectionalLightDesc.h>
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
// The level is checked twice. Against Blender's Cycles pixels directly, because Cycles is the exact
// cosine integral of the source and the tone map is now Blender's own LUT, so the two frames should
// agree; and against that integral pushed through the shipped tone map by RunAgX, which separates a
// lighting fault from a tone-map one when the first check fails. A 1.34 exposure, the normalization
// this asset used to ship with, moves these by about 0.06; the polynomial fit the tone map used to
// be sat 0.03 above Blender here.
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
//   Eevee, Lambert      left 0.4281   right 0.4945   Eevee's world diffuse is an L1 harmonic: flatter
//
// Eevee's row is what the Material Preview shows and is not asserted: the difference is Blender's
// first-order approximation of the source, measured, and not a term to match.

namespace
{
	constexpr uint32_t c_Width  = 400;
	constexpr uint32_t c_Height = 300;

	constexpr int c_BoxSize = 16;

	// The sphere's limbs, and two backdrop corners, at the pixels blender_probe.py reads.
	constexpr int c_SphereCentreX = 192;
	constexpr int c_SphereLeftX   = 141;
	constexpr int c_SphereRightX  = 243;
	constexpr int c_SphereY       = 142;
	constexpr int c_SkyLeftX      = 10;
	constexpr int c_SkyRightX     = 374;
	constexpr int c_SkyY          = 20;

	// Irradiance of the source at the normal under each limb box, (+-0.632, 0, 0.775): the cosine
	// integral of forest.exr in the map's 1/pi convention, as blender_probe.py prints it under
	// `irradiance`. Bernini's +X is the screen's right.
	constexpr float c_Albedo          = 0.18f;
	constexpr float c_IrradianceRight = 1.4543f;
	constexpr float c_IrradianceLeft  = 0.7371f;

	// Display luma Blender's Cycles rendered over the sphere's limbs and the backdrop corners.
	constexpr float c_BlenderSphereLeft  = 0.3973f;
	constexpr float c_BlenderSphereRight = 0.5406f;
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

	const float expectedLeft =
		bgl::test::EncodeSrgb(bgl::test::RunAgX(*gfx, c_Albedo * c_IrradianceLeft).r);
	const float expectedRight =
		bgl::test::EncodeSrgb(bgl::test::RunAgX(*gfx, c_Albedo * c_IrradianceRight).r);

	INFO(
		"sphere L/R " << sphereLeft.Luma() << "/" << sphereRight.Luma() << " expected "
					  << expectedLeft << "/" << expectedRight << ", Blender's L/R "
					  << c_BlenderSphereLeft << "/" << c_BlenderSphereRight);
	INFO(
		"sky L/R " << skyLeft.Luma() << "/" << skyRight.Luma() << " against Blender's L/R "
				   << c_BlenderSkyLeft << "/" << c_BlenderSkyRight);

	CHECK(std::abs(skyLeft.Luma() - c_BlenderSkyLeft) < c_SkyMargin);
	CHECK(std::abs(skyRight.Luma() - c_BlenderSkyRight) < c_SkyMargin);

	CHECK(std::abs(sphereLeft.Luma() - c_BlenderSphereLeft) < c_LevelMargin);
	CHECK(std::abs(sphereRight.Luma() - c_BlenderSphereRight) < c_LevelMargin);

	CHECK(std::abs(sphereLeft.Luma() - expectedLeft) < c_LevelMargin);
	CHECK(std::abs(sphereRight.Luma() - expectedRight) < c_LevelMargin);
}

// The sun's own level, against the same renderer and through the same instrument as the case above.
//
// This is the one measurement in the feature that is not Bernini checked against Bernini's own
// algebra. Tasks 1 and 2 assert the shader against arithmetic written beside it, and a pi dropped in
// both the shader and the test passes them both; Cycles is an independent integrator that knows
// nothing about either.
//
// What it pins is the unit conversion. Blender's Sun strength is irradiance in W/m^2 on a surface
// facing it. Bernini's intensity is what its irradiance map would hold, and that map carries E/pi.
// So blender_probe.py sets a strength of pi times the number handed to SetDirectionalLight, and both
// renderers should then put `albedo * intensity * NdotL` on screen. Either side losing the factor
// moves this by a stop and nothing else in the suite would notice.
//
// The environment is black, so the sun is the only light in the frame and the split-sum path -- the
// one place the two renderers genuinely differ -- contributes nothing.
//
// What Blender 5.2.1 measured, display luma over the sphere's limb boxes (blender_probe.py,
// `--engine CYCLES --samples 64 --sun 0.6 --sun-azimuth 0 --sun-elevation 0 --no-world
// --albedo 0.5`):
//
//   Cycles, Lambert     left 0.5128   right 0.5129   sun irradiance 0.465 at both normals
//
// Equal at both limbs because a sun head on to the camera makes the same angle with each, which the
// environment case never does -- so it is also a check that nothing has put a gradient in the frame.
TEST_CASE(
	"A matte sphere under a sun alone sits at Blender's level",
	"[pbr][light][parity][render]")
{
	constexpr float c_SunAlbedo    = 0.5f;
	constexpr float c_SunIntensity = 0.6f;

	// intensity * NdotL at (+-0.632, 0, 0.775) against a sun along +Z, as the probe prints it.
	constexpr float c_SunIrradiance = 0.465f;

	constexpr float c_BlenderSunLeft  = 0.5128f;
	constexpr float c_BlenderSunRight = 0.5129f;

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

	bgl::test::ApplyBlackEnvironment(scene.Get(), view.Get());

	// Along +Z toward the camera, which is azimuth 0 elevation 0 in the probe's convention.
	view->SetDirectionalLight(
		{ .direction = glm::vec3(0.0f, 0.0f, -1.0f),
	      .color     = glm::vec3(1.0f),
	      .intensity = c_SunIntensity });

	const auto matte = scene->CreatePbrMaterial(
		{ .baseColorFactor = glm::vec4(c_SunAlbedo, c_SunAlbedo, c_SunAlbedo, 1.0f),
	      .metallicFactor  = 0.0f,
	      .roughnessFactor = 1.0f,
	      .specularFactor  = 0.0f });

	const auto sphere   = scene->AddSphereGeom(64, 32, 5.0f, matte);
	const auto instance = view->CreateStaticMeshInstance(sphere, glm::mat4(1.0f));

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

	const std::string shot = "assets/golden/blender_parity_sun.got.png";
	gfx->ScreenshotPng(target, shot);

	const auto left  = bgl::test::MeanColor(shot, c_SphereLeftX, c_SphereY, c_BoxSize, c_BoxSize);
	const auto right = bgl::test::MeanColor(shot, c_SphereRightX, c_SphereY, c_BoxSize, c_BoxSize);

	const float expected =
		bgl::test::EncodeSrgb(bgl::test::RunAgX(*gfx, c_SunAlbedo * c_SunIrradiance).r);

	INFO(
		"sun L/R " << left.Luma() << "/" << right.Luma() << " expected " << expected
				   << ", Blender's L/R " << c_BlenderSunLeft << "/" << c_BlenderSunRight);

	CHECK(std::abs(left.Luma() - c_BlenderSunLeft) < c_LevelMargin);
	CHECK(std::abs(right.Luma() - c_BlenderSunRight) < c_LevelMargin);

	CHECK(std::abs(left.Luma() - expected) < c_LevelMargin);
	CHECK(std::abs(right.Luma() - expected) < c_LevelMargin);

	// The specular lobe's own level, measured where the half vector meets the normal -- the sphere's
	// centre, since the limb boxes sit far enough down the lobe to measure almost none of it.
	//
	// The same margin as the diffuse case, because the same margin is what the measurement asks for:
	// the two agree to 0.003 here.
	//
	// The two renderers do genuinely differ, and it is worth knowing where. Blender's Principled BSDF
	// is layered, so turning its specular up takes that energy out of the diffuse underneath;
	// Bernini's direct diffuse is split by `1 - metallic` alone (ADR-10 of the plan) and the lobe is
	// added on top, so a dielectric keeps a diffuse Blender has already spent. That lands where the
	// diffuse dominates -- at the limbs Cycles drops 0.5128 to 0.4988 with the specular on and
	// Bernini does not move at all, about 0.014 of display luma. It does not land at the peak, which
	// is what this box measures and where the lobe is most of the answer. A case placed in the middle
	// of that divergence would need a margin sized for it; this one does not, and widening this one
	// on its behalf would only hide a regression here.
	//
	// What Blender 5.2.1 measured (`--samples 256 --sun 0.6 --no-world --albedo 0.5 --roughness 0.3
	// --specular 0.5`, whose Specular IOR Level 0.5 is F0 = 0.04, the same dielectric Bernini's
	// specularFactor 1.0 gives):
	//
	//   Cycles     centre 0.672   left 0.4988   right 0.4989
	constexpr float c_BlenderSpecCentre = 0.672f;

	// The same sphere repainted, not a second one: two coincident spheres z-fight and the test would
	// be measuring whichever won.
	view->SetSubmeshMaterialOverride(
		instance,
		0,
		scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(c_SunAlbedo, c_SunAlbedo, c_SunAlbedo, 1.0f),
	          .metallicFactor  = 0.0f,
	          .roughnessFactor = 0.3f,
	          .specularFactor  = 1.0f }));

	for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);

	const std::string specShot = "assets/golden/blender_parity_sun_specular.got.png";
	gfx->ScreenshotPng(target, specShot);

	const auto centre =
		bgl::test::MeanColor(specShot, c_SphereCentreX, c_SphereY, c_BoxSize, c_BoxSize);

	INFO("specular centre " << centre.Luma() << " against Blender's " << c_BlenderSpecCentre);
	CHECK(std::abs(centre.Luma() - c_BlenderSpecCentre) < c_LevelMargin);
}
