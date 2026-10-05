#include "util/AgxProbe.h"
#include "util/GoldenImage.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>

// The post-process a target ends in: toon is the exposed value clamped and sRGB-encoded, as
// Blender's Standard view shows it, and filmic is the AgX it always was. A full-frame Unlit plane puts a
// known radiance on every pixel, so the frame is the curve's answer for it and nothing else.

namespace
{
	constexpr int c_Size = 64;

	bgl::test::GraphicsSetup
	Options()
	{
		auto opts                       = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir  = bgl::test::ShaderCacheDir();
		opts.gpuContext.clientShaderDir = "./shaders/tests/surfaces";
		return opts;
	}

	struct Plane
	{
		bgl::GraphicsRef     gfx = bgl::test::CreateGraphics(Options());
		bgl::SceneRef        scene;
		bgl::RenderTargetRef target;

		Plane()
		{
			REQUIRE(gfx != nullptr);
			scene = gfx->CreateScene(bgl::SceneDesc());

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = c_Size;
			targetDesc.height   = c_Size;
			targetDesc.headless = true;
			target              = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);
		}

		/** A frame of one radiance, `exposure` applied; its mean colour. */
		bgl::test::Rgba
		Shoot(const glm::vec3& radiance, float exposure, const std::string& png)
		{
			const auto material = scene->CreateSurfaceMaterial(
				bgl::SurfaceMaterialDesc{ .surfaceName = "Unlit",
			                              .values      = { { "color", glm::vec4(radiance, 0.0f) },
			                                               { "opacity", glm::vec4(1.0f) } } });
			auto view = gfx->CreateSceneView(scene, 4);
			view->SetExposure(exposure);
			view->CreateStaticMeshInstance(
				bgl::StaticMeshInstanceDesc().SetGeom(
					scene->AddPlaneGeom(1, 1, 40.0f, 40.0f, material)));

			auto job     = bgl::RenderJob();
			job.view     = view;
			job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));
			job.camera   = bgl::Camera()
			                   .LookAt(
								   glm::vec3(0.0f, 0.0f, 5.0f),
								   glm::vec3(0.0f),
								   glm::vec3(0.0f, 1.0f, 0.0f))
			                   .Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
			for (int i = 0; i < 2; ++i) gfx->DrawFrame(target, job);
			gfx->ScreenshotPng(target, png);
			return bgl::test::MeanColor(png, 16, 16, 32, 32);
		}
	};
}

TEST_CASE("Toon post-process shows the exposed value clamped and sRGB-encoded", "[tonemap][render]")
{
	Plane plane;
	CHECK(plane.target->GetPostProcessType() == bgl::PostProcessType::kFilmic);
	plane.target->SetPostProcessType(bgl::PostProcessType::kToon);
	CHECK(plane.target->GetPostProcessType() == bgl::PostProcessType::kToon);

	// One quantization step, and a little for the 16-bit scene colour on the way.
	constexpr float c_Margin = 1.5f / 255.0f;

	const std::array<std::pair<glm::vec3, float>, 4> cases = { {
		{ glm::vec3(0.18f, 0.5f, 0.9f), 1.0f },
		{ glm::vec3(0.8f, 0.35f, 0.1f), 1.0f },
		{ glm::vec3(0.4f, 0.02f, 1.6f), 1.0f },
		{ glm::vec3(0.3f, 0.6f, 0.05f), 2.0f },
	} };

	for (size_t i = 0; i < cases.size(); ++i)
	{
		const auto& [radiance, exposure] = cases[i];
		INFO("case " << i);
		const auto got = plane.Shoot(
			radiance,
			exposure,
			"assets/golden/tonemap_standard_" + std::to_string(i) + ".got.png");
		const glm::vec3 exposed = glm::clamp(radiance * exposure, 0.0f, 1.0f);
		CHECK(got.r == Catch::Approx(bgl::test::EncodeSrgb(exposed.r)).margin(c_Margin));
		CHECK(got.g == Catch::Approx(bgl::test::EncodeSrgb(exposed.g)).margin(c_Margin));
		CHECK(got.b == Catch::Approx(bgl::test::EncodeSrgb(exposed.b)).margin(c_Margin));
	}
}

TEST_CASE(
	"A target switches its post-process between frames, and filmic is what it was",
	"[tonemap][render]")
{
	Plane           plane;
	const glm::vec3 radiance(0.8f, 0.35f, 0.1f);

	const auto* agx      = "assets/golden/tonemap_switch_agx.got.png";
	const auto* standard = "assets/golden/tonemap_switch_standard.got.png";
	const auto* back     = "assets/golden/tonemap_switch_back.got.png";

	plane.Shoot(radiance, 1.0f, agx);
	plane.target->SetPostProcessType(bgl::PostProcessType::kToon);
	plane.Shoot(radiance, 1.0f, standard);
	plane.target->SetPostProcessType(bgl::PostProcessType::kFilmic);
	plane.Shoot(radiance, 1.0f, back);

	CHECK(bgl::test::MaxChannelDelta(agx, standard) > 0.05f);
	CHECK(bgl::test::MaxChannelDelta(agx, back) == 0.0f);

	// A target asked for toon at creation starts in it.
	auto targetDesc            = bgl::RenderTargetDesc();
	targetDesc.width           = c_Size;
	targetDesc.height          = c_Size;
	targetDesc.headless        = true;
	targetDesc.postProcessType = bgl::PostProcessType::kToon;
	plane.target               = plane.gfx->CreateRenderTarget(targetDesc);
	CHECK(plane.target->GetPostProcessType() == bgl::PostProcessType::kToon);
	const auto* created = "assets/golden/tonemap_switch_created.got.png";
	plane.Shoot(radiance, 1.0f, created);
	CHECK(bgl::test::MaxChannelDelta(standard, created) == 0.0f);
}

// A full-frame plane of one radiance blurs to itself, so with the threshold at zero the chain's
// level is that radiance exactly and the frame is the combine's answer for it.
TEST_CASE(
	"Toon screens the glow, so a bright colour stops short of white",
	"[tonemap][bloom][render]")
{
	Plane plane;
	plane.target->SetPostProcessType(bgl::PostProcessType::kToon);

	auto bloom      = bgl::BloomSettings();
	bloom.threshold = 0.0f;
	plane.target->SetBloomEnabled(true);

	constexpr float c_Margin = 1.5f / 255.0f;

	struct Case
	{
		glm::vec3 radiance;
		float     intensity;
	};

	// The second is the one an add clips: 0.9 + 0.9 and 0.6 + 0.6 both land on white, and the
	// orange goes yellow. The third's red is past the display's range before any glow.
	const std::array<Case, 3> cases = { {
		{ glm::vec3(0.5f, 0.25f, 0.8f), 0.5f },
		{ glm::vec3(0.9f, 0.6f, 0.3f), 1.0f },
		{ glm::vec3(2.0f, 0.1f, 0.05f), 0.25f },
	} };

	for (size_t i = 0; i < cases.size(); ++i)
	{
		const auto& [radiance, intensity] = cases[i];
		INFO("case " << i);

		bloom.intensity = intensity;
		plane.target->SetBloomSettings(bloom);

		const auto got = plane.Shoot(
			radiance,
			1.0f,
			"assets/golden/tonemap_toon_glow_" + std::to_string(i) + ".got.png");

		const glm::vec3 base     = glm::clamp(radiance, 0.0f, 1.0f);
		const glm::vec3 glow     = glm::clamp(radiance * intensity, 0.0f, 1.0f);
		const glm::vec3 expected = base + glow * (1.0f - base);

		CHECK(got.r == Catch::Approx(bgl::test::EncodeSrgb(expected.r)).margin(c_Margin));
		CHECK(got.g == Catch::Approx(bgl::test::EncodeSrgb(expected.g)).margin(c_Margin));
		CHECK(got.b == Catch::Approx(bgl::test::EncodeSrgb(expected.b)).margin(c_Margin));
	}

	// The orange kept its order, where an add lands red and green both on white.
	bloom.intensity = 1.0f;
	plane.target->SetBloomSettings(bloom);
	const auto orange = plane.Shoot(
		glm::vec3(0.9f, 0.6f, 0.3f),
		1.0f,
		"assets/golden/tonemap_toon_glow_orange.got.png");
	CHECK(orange.g < 0.95f);
	CHECK(orange.g < orange.r - 0.02f);
}

// The grade reaches a toon frame through the target, and a black plane makes the frame the grade's
// answer for black: its offset, which no curve stands between.
TEST_CASE("A toon target is graded on the value it displays", "[tonemap][colorgrade][render]")
{
	Plane plane;
	plane.target->SetPostProcessType(bgl::PostProcessType::kToon);

	constexpr float c_Margin = 1.5f / 255.0f;

	const auto* plain   = "assets/golden/tonemap_toon_grade_plain.got.png";
	const auto* neutral = "assets/golden/tonemap_toon_grade_neutral.got.png";
	const auto* lifted  = "assets/golden/tonemap_toon_grade_lifted.got.png";
	const auto* off     = "assets/golden/tonemap_toon_grade_off.got.png";

	const glm::vec3 teal(0.1f, 0.4f, 0.6f);

	plane.Shoot(teal, 1.0f, plain);
	plane.target->SetColorGradeEnabled(true);
	plane.Shoot(teal, 1.0f, neutral);
	CHECK(bgl::test::MaxChannelDelta(plain, neutral) <= 1.0f / 255.0f);

	auto grade   = bgl::ColorGradeSettings();
	grade.offset = glm::vec3(0.0f, 0.055f, 0.05f);
	plane.target->SetColorGradeSettings(grade);

	const auto got = plane.Shoot(glm::vec3(0.0f), 1.0f, lifted);
	CHECK(got.r == Catch::Approx(0.0f).margin(c_Margin));
	CHECK(got.g == Catch::Approx(0.055f).margin(c_Margin));
	CHECK(got.b == Catch::Approx(0.05f).margin(c_Margin));

	plane.target->SetColorGradeEnabled(false);
	const auto black = plane.Shoot(glm::vec3(0.0f), 1.0f, off);
	CHECK(black.g == Catch::Approx(0.0f).margin(c_Margin));
}
