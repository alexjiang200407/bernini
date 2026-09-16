#include "util/AgxProbe.h"
#include "util/GoldenImage.h"
#include "util/SyntheticCube.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <assetlib/envmap.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/Viewport.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

// The sun has no reference image behind it: what it emits is an equation, and these cases assert
// that equation and nothing else. A flat plane is the fixture rather than the sphere every other
// lighting test uses, because a plane's normal is constant over the whole sample box -- so N dot L
// is exact, and a disagreement is the shader being wrong rather than the box averaging a curve.
//
// The environment is black, so the sun is the only light in the frame, and the material's specular
// factor is 0, so the split-sum path contributes nothing either. What lands on screen is
// `albedo * radiance * NdotL` through the shipped tone map, and RunAgX is what says where that is --
// the same instrument BlenderParity_test uses, and the only one that cannot drift from the shader.
//
// A coloured sun is asserted by ordering rather than by level: AgX mixes channels, so RunAgX's
// single scene-linear argument cannot predict a non-grey result channel by channel.

namespace
{
	constexpr uint32_t c_Width  = 400;
	constexpr uint32_t c_Height = 300;

	// EnvOrientation_test's cube shape: a 7-mip prefilter chain is MAX_REFLECTION_LOD + 1.
	constexpr uint32_t c_SourceFace     = 64;
	constexpr uint32_t c_IrradianceFace = 32;
	constexpr uint32_t c_PrefilterMips  = 7;

	// A 16x16 box on the middle of the plane, which covers the whole frame at this camera.
	constexpr int c_BoxSize = 16;
	constexpr int c_BoxX    = 192;
	constexpr int c_BoxY    = 142;

	constexpr float c_Albedo    = 0.5f;
	constexpr float c_Intensity = 0.6f;

	// BlenderParity_test's margin, in display luma, and for the same reason: below this the tone
	// map's own quantization is what is being measured.
	constexpr float c_LevelMargin = 0.015f;

	struct Probe
	{
		bgl::GraphicsRef     gfx;
		bgl::RenderTargetRef target;
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
	};

	/**
	 * A matte plane at the origin facing the camera, with no light on it at all: the environment is
	 * black and the material reflects nothing specularly, so every photon in the frame comes from
	 * whatever SetDirectionalLight is handed next.
	 */
	Probe
	MakeProbe(bool blackEnvironment)
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;

		auto probe = Probe();
		probe.gfx  = bgl::CreateGraphics(opts);
		REQUIRE(probe.gfx != nullptr);

		auto targetDesc     = bgl::RenderTargetDesc();
		targetDesc.width    = static_cast<int>(c_Width);
		targetDesc.height   = static_cast<int>(c_Height);
		targetDesc.headless = true;
		probe.target        = probe.gfx->CreateRenderTarget(targetDesc);
		REQUIRE(probe.target != nullptr);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 4;
		sceneDesc.initialMeshlets             = 512;
		sceneDesc.initialSubmeshes            = 4;
		sceneDesc.initialVertexBufferByteSize = 400000;
		sceneDesc.initialIndices              = 20000;
		sceneDesc.initialPbrMaterials         = 4;

		probe.scene = probe.gfx->CreateScene(sceneDesc);
		probe.view  = probe.gfx->CreateSceneView(probe.scene, 4);

		if (blackEnvironment)
		{
			// Convolved rather than fabricated: black through the real bake is guaranteed to have the
			// shape the sampler expects, and a hand-built mip chain is one more thing to get wrong.
			auto prefilterDesc      = assetlib::PrefilterDesc();
			prefilterDesc.faceSize  = c_SourceFace;
			prefilterDesc.mipLevels = c_PrefilterMips;
			prefilterDesc.samples   = 32;

			const auto radiance = bgl::test::MakeBlackFloatCube(c_SourceFace);

			probe.view->SetEnvironmentMap(
				{ probe.scene->AddTextureAsset(
					  assetlib::irradianceSh(radiance, c_IrradianceFace),
					  "black_irradiance"),
			      probe.scene->AddTextureAsset(
					  assetlib::prefilterRadiance(radiance, prefilterDesc, nullptr),
					  "black_prefilter") });

			probe.view->SetExposure(1.0f);
		}
		else
		{
			bgl::test::ApplyEnvironment(probe.scene.Get(), probe.view.Get());
		}

		// specularFactor 0 removes the split-sum lobe entirely: F0 is zeroed and the weight with it,
		// so the frame carries the diffuse term alone and nothing has to be subtracted from it.
		const auto matte = probe.scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(c_Albedo, c_Albedo, c_Albedo, 1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 1.0f,
		      .specularFactor  = 0.0f });

		// Normal +Z, facing the camera, and wider than the frustum at this distance.
		const auto plane = probe.scene->AddPlaneGeom(1, 1, 40.0f, 40.0f, matte);
		(void)probe.view->CreateStaticMeshInstance(plane, glm::mat4(1.0f));

		return probe;
	}

	/** Renders the probe and returns the mean colour of the sample box. */
	bgl::test::Rgba
	Shoot(Probe& probe, const std::string& name)
	{
		auto camera = bgl::Camera();
		camera.LookAt(glm::vec3(0.0f, 0.0f, 20.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);

		auto job     = bgl::RenderJob();
		job.view     = probe.view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		// More frames than a render test usually needs. The scene is static and the camera never
		// moves, so TAA reprojects perfectly and the previous shot's lighting stays in the history
		// until it is blended out -- and each case here changes the sun and shoots again.
		for (int i = 0; i < 24; ++i) probe.gfx->DrawFrame(probe.target, job);

		const auto shot = "assets/golden/directional_light_" + name + ".got.png";
		probe.gfx->ScreenshotPng(probe.target, shot);

		return bgl::test::MeanColor(shot, c_BoxX, c_BoxY, c_BoxSize, c_BoxSize);
	}

	/** Where a scene-linear grey lands on screen, as the shipped tone map puts it. */
	float
	OnScreen(bgl::IGraphics& gfx, float sceneLinear)
	{
		return bgl::test::EncodeSrgb(bgl::test::RunAgX(gfx, sceneLinear).r);
	}
}

TEST_CASE("A directional light lights a surface by the cosine of its angle", "[pbr][light][render]")
{
	auto probe = MakeProbe(true);

	const auto sunlit = [&probe](glm::vec3 direction, glm::vec3 color, float intensity) {
		probe.view->SetDirectionalLight(
			{ .direction = direction, .color = color, .intensity = intensity });
	};

	// The plane's normal is +Z and the camera looks down -Z at it, so a sun travelling -Z arrives
	// exactly head on. If `direction` meant "the direction the sun is in" rather than the direction
	// it travels, this is the case that would come out black -- see DirectionalLightDesc.h.
	sunlit(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(1.0f), c_Intensity);
	const auto head         = Shoot(probe, "head_on");
	const auto expectedHead = OnScreen(*probe.gfx, c_Albedo * c_Intensity);

	INFO("head on " << head.Luma() << " expected " << expectedHead);
	CHECK(std::abs(head.Luma() - expectedHead) < c_LevelMargin);

	// 45 degrees off the normal in the XZ plane: NdotL is 1/sqrt(2), and nothing else about the
	// frame has changed, so this is the cosine and only the cosine.
	constexpr float c_Cos45 = 0.70710678f;
	sunlit(glm::normalize(glm::vec3(-1.0f, 0.0f, -1.0f)), glm::vec3(1.0f), c_Intensity);
	const auto slanted         = Shoot(probe, "slanted");
	const auto expectedSlanted = OnScreen(*probe.gfx, c_Albedo * c_Intensity * c_Cos45);

	INFO("slanted " << slanted.Luma() << " expected " << expectedSlanted);
	CHECK(std::abs(slanted.Luma() - expectedSlanted) < c_LevelMargin);
	CHECK(slanted.Luma() < head.Luma());

	// Grazing incidence, 85 degrees off the normal: NdotL is small and *positive*, which is the end
	// of the range a dropped clamp or a flipped sign hides in -- both read as plausibly dark here,
	// and both are caught by asserting the level rather than the ordering.
	//
	// It lands further from its expectation than the others -- 0.007 against 0.001 head-on -- and
	// that is the margin biting harder here, not more loosely. The tone map's slope at this level is
	// around 3.3 against 0.8 to 1.5 at the levels BlenderParity checks, so the same scene-linear
	// error shows up several times larger in display luma, and `c_LevelMargin` constrains the
	// underlying quantity more tightly than it does anywhere else in this file. What runs out past
	// 85 degrees is absolute headroom, at which point this wants a relative margin.
	constexpr float c_GrazingDeg = 85.0f;
	const float     grazingCos   = std::cos(glm::radians(c_GrazingDeg));
	const float     grazingSin   = std::sin(glm::radians(c_GrazingDeg));

	sunlit(glm::vec3(-grazingSin, 0.0f, -grazingCos), glm::vec3(1.0f), c_Intensity);
	const auto grazing         = Shoot(probe, "grazing");
	const auto expectedGrazing = OnScreen(*probe.gfx, c_Albedo * c_Intensity * grazingCos);

	INFO("grazing " << grazing.Luma() << " expected " << expectedGrazing);
	CHECK(std::abs(grazing.Luma() - expectedGrazing) < c_LevelMargin);
	CHECK(grazing.Luma() < slanted.Luma());

	// Facing away is zero and not a negative that something downstream clamps: the same box, the
	// same plane, lit to the tone map's floor. `head` above is what proves the box is on geometry.
	sunlit(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f), c_Intensity);
	const auto behind         = Shoot(probe, "behind");
	const auto expectedBehind = OnScreen(*probe.gfx, 0.0f);

	INFO("behind " << behind.Luma() << " expected " << expectedBehind);
	CHECK(std::abs(behind.Luma() - expectedBehind) < c_LevelMargin);

	// Colour and intensity multiply, and only their product reaches the shader.
	sunlit(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.5f), c_Intensity * 2.0f);
	const auto halved = Shoot(probe, "halved");

	INFO("half colour at double intensity " << halved.Luma() << " against " << head.Luma());
	CHECK(std::abs(halved.Luma() - head.Luma()) < c_LevelMargin);

	// A red sun on a white plane, asserted by ordering: AgX mixes channels, so there is no
	// scene-linear grey that predicts this one channel by channel.
	sunlit(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(1.0f, 0.25f, 0.25f), c_Intensity);
	const auto tinted = Shoot(probe, "tinted");

	INFO("tinted rgb " << tinted.r << " " << tinted.g << " " << tinted.b);
	CHECK(tinted.r > tinted.g);
	CHECK(std::abs(tinted.g - tinted.b) < c_LevelMargin);
}

TEST_CASE("A directional light adds to the environment beside it", "[pbr][light][render]")
{
	auto probe = MakeProbe(false);

	// The environment alone, which is every frame this engine rendered before there was a sun: a
	// view that never sets one is this, because the default intensity is 0.
	const auto ibl = Shoot(probe, "ibl_only");

	probe.view->SetDirectionalLight(bgl::DirectionalLightDesc());
	const auto defaulted = Shoot(probe, "ibl_default_sun");

	INFO("default sun " << defaulted.Luma() << " against no sun " << ibl.Luma());
	CHECK(std::abs(defaulted.Luma() - ibl.Luma()) < c_LevelMargin);

	probe.view->SetDirectionalLight(
		{ .direction = glm::vec3(0.0f, 0.0f, -1.0f),
	      .color     = glm::vec3(1.0f),
	      .intensity = c_Intensity });
	const auto lit = Shoot(probe, "ibl_and_sun");

	INFO("environment and sun " << lit.Luma() << " against environment alone " << ibl.Luma());
	CHECK(lit.Luma() > ibl.Luma());
}

TEST_CASE("SetDirectionalLight refuses a light that cannot be shaded", "[light]")
{
	auto probe = MakeProbe(true);

	constexpr auto c_Nan = std::numeric_limits<float>::quiet_NaN();

	// Normalizing this yields NaN, and there is no direction to fall back on -- a sun pointing
	// nowhere is a caller that forgot to set one, not a sun that is switched off.
	CHECK_THROWS_AS(
		probe.view->SetDirectionalLight({ .direction = glm::vec3(0.0f) }),
		bgl::SceneError);

	CHECK_THROWS_AS(
		probe.view->SetDirectionalLight(
			{ .direction = glm::vec3(0.0f, -1.0f, 0.0f), .intensity = -1.0f }),
		bgl::SceneError);

	CHECK_THROWS_AS(
		probe.view->SetDirectionalLight({ .direction = glm::vec3(c_Nan, -1.0f, 0.0f) }),
		bgl::SceneError);

	CHECK_THROWS_AS(
		probe.view->SetDirectionalLight(
			{ .direction = glm::vec3(0.0f, -1.0f, 0.0f), .color = glm::vec3(c_Nan) }),
		bgl::SceneError);

	CHECK_THROWS_AS(
		probe.view->SetDirectionalLight(
			{ .direction = glm::vec3(0.0f, -1.0f, 0.0f), .intensity = c_Nan }),
		bgl::SceneError);
}
