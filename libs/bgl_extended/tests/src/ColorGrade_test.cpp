#include "util/AgxProbe.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/RenderJob.h>
#include <bgl/SkyboxDesc.h>
#include <bgl/Viewport.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

// The colour grade: its settings are validated where every target setting is, a neutral grade is
// the ungraded image, and each control moves the output the way its name says. The directions are
// read through CSColorGradeProbe, which runs the post pass's own grade code on one colour; the
// render cases prove the settings reach that code through the target.

namespace
{
	constexpr uint32_t c_Size = 256;

	bgl::GraphicsRef
	MakeGraphics()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);
		return gfx;
	}

	const auto c_Centre = glm::vec2(0.5f);

	// Larger than the LUT's own trilinear wobble and float noise, far smaller than any control
	// moved by the amounts below.
	constexpr float c_Moved = 0.005f;

	glm::vec3
	Graded(bgl::IGraphics& gfx, glm::vec3 color, const bgl::ColorGradeSettings& settings)
	{
		return glm::vec3(bgl::test::RunGradedAgX(gfx, color, c_Centre, settings));
	}

	float
	Luma(glm::vec3 c)
	{
		return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
	}

	float
	Spread(glm::vec3 c)
	{
		return glm::max(c.r, glm::max(c.g, c.b)) - glm::min(c.r, glm::min(c.g, c.b));
	}

	/**
	 * An orange, fairly glossy cube in front of the shipped environment's sky at its baked exposure:
	 * colour to desaturate, a sky to cover every other pixel, and the brightness the editor shows.
	 */
	struct OrangeCube
	{
		bgl::GraphicsRef     gfx;
		bgl::RenderTargetRef target;
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
		bgl::RenderJob       job;

		explicit OrangeCube()
		{
			gfx = MakeGraphics();

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = static_cast<int>(c_Size);
			targetDesc.height   = static_cast<int>(c_Size);
			targetDesc.headless = true;
			target              = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);

			auto sceneDesc                        = bgl::SceneDesc();
			sceneDesc.initialGeom                 = 4;
			sceneDesc.initialMeshlets             = 64;
			sceneDesc.initialSubmeshes            = 4;
			sceneDesc.initialVertexBufferByteSize = 40000;
			sceneDesc.initialIndices              = 1000;
			sceneDesc.initialPbrMaterials         = 4;

			scene = gfx->CreateScene(sceneDesc);
			view  = gfx->CreateSceneView(scene, 4);
			bgl::test::ApplyEnvironment(scene.Get(), view.Get());
			view->SetSkyBox(bgl::SkyboxDesc{ bgl::test::LoadSkybox(scene.Get()) });

			const auto orange = scene->CreatePbrMaterial(
				{ .baseColorFactor = glm::vec4(0.9f, 0.35f, 0.08f, 1.0f),
			      .metallicFactor  = 0.0f,
			      .roughnessFactor = 0.4f });

			view->CreateStaticMeshInstance(scene->AddCubeGeom(orange), glm::mat4(1.0f));

			auto camera = bgl::Camera();
			camera
				.LookAt(
					glm::vec3(1.5f, 1.2f, 3.5f),
					glm::vec3(0.0f, 0.0f, 0.0f),
					glm::vec3(0.0f, 1.0f, 0.0f))
				.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);

			job.view     = view;
			job.camera   = camera;
			job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));
		}

		void
		Capture(const std::string& path)
		{
			// Two frames: the first uploads and presents, the screenshot reads the last presented.
			gfx->DrawFrame(target, job);
			gfx->DrawFrame(target, job);
			gfx->ScreenshotPng(target, path);
		}

		[[nodiscard]] static bgl::test::Rgba
		CentreProbe(const std::string& path)
		{
			const int origin = static_cast<int>(c_Size) / 2 - 8;
			return bgl::test::MeanColor(path, origin, origin, 16, 16);
		}
	};
}

TEST_CASE("Colour grade settings outside their documented ranges are refused", "[colorgrade]")
{
	auto gfx = MakeGraphics();

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 64;
	targetDesc.height   = 64;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	CHECK(!target->IsColorGradeEnabled());

	auto kept     = bgl::ColorGradeSettings();
	kept.contrast = 1.2f;
	target->SetColorGradeSettings(kept);

	constexpr float c_Nan = std::numeric_limits<float>::quiet_NaN();
	constexpr float c_Inf = std::numeric_limits<float>::infinity();

	const auto refuses = [&](auto mutate) {
		auto bad = bgl::ColorGradeSettings();
		mutate(bad);
		CHECK_THROWS_AS(target->SetColorGradeSettings(bad), bgl::GraphicsError);
	};

	refuses([](auto& s) { s.temperature = 100.5f; });
	refuses([](auto& s) { s.temperature = c_Nan; });
	refuses([](auto& s) { s.tint = -101.0f; });
	refuses([](auto& s) { s.slope.g = -0.1f; });
	refuses([](auto& s) { s.slope.b = c_Inf; });
	refuses([](auto& s) { s.offset.r = 1.5f; });
	refuses([](auto& s) { s.offset.b = c_Nan; });
	refuses([](auto& s) { s.power.r = 0.0f; });
	refuses([](auto& s) { s.power.g = c_Inf; });
	refuses([](auto& s) { s.saturation = -0.5f; });
	refuses([](auto& s) { s.contrast = c_Nan; });
	refuses([](auto& s) { s.vignetteIntensity = 1.1f; });
	refuses([](auto& s) { s.vignetteSmoothness = 0.0f; });

	// A refused set leaves the settings the target had.
	CHECK(target->GetColorGradeSettings().contrast == 1.2f);

	// The edges of every range are in it.
	auto edges               = bgl::ColorGradeSettings();
	edges.temperature        = -100.0f;
	edges.tint               = 100.0f;
	edges.slope              = glm::vec3(0.0f);
	edges.offset             = glm::vec3(-1.0f, 1.0f, 0.0f);
	edges.saturation         = 0.0f;
	edges.contrast           = 0.0f;
	edges.vignetteIntensity  = 1.0f;
	edges.vignetteSmoothness = 1.0f;
	CHECK_NOTHROW(target->SetColorGradeSettings(edges));
}

/**
 * The neutral grade is AgX exactly, across the range a scene spans: the log encode, the formation
 * and the linearization are the same functions the ungraded path runs, and every control is its
 * identity. A dropped linearization or a contrast pivot off middle grey fails here first.
 */
TEST_CASE("A neutral grade leaves the tone map's output where it was", "[colorgrade][tonemap]")
{
	auto gfx = MakeGraphics();

	constexpr std::array<float, 4> c_Sweep = { { 0.01f, 0.18f, 1.0f, 8.0f } };

	for (const float v : c_Sweep)
	{
		const glm::vec4 plain  = bgl::test::RunAgX(*gfx, v);
		const glm::vec3 graded = Graded(*gfx, glm::vec3(v), bgl::ColorGradeSettings());

		INFO("scene-linear " << v << ": " << plain.r << " vs " << graded.r);
		CHECK(graded.r == Catch::Approx(plain.r).margin(1e-4));
		CHECK(graded.g == Catch::Approx(plain.g).margin(1e-4));
		CHECK(graded.b == Catch::Approx(plain.b).margin(1e-4));
	}
}

TEST_CASE("The CDL and contrast move the image the way they are named", "[colorgrade][tonemap]")
{
	auto gfx = MakeGraphics();

	const auto grey    = glm::vec3(0.18f);
	const auto orange  = glm::vec3(0.6f, 0.25f, 0.08f);
	const auto neutral = bgl::ColorGradeSettings();

	const glm::vec3 greyOut   = Graded(*gfx, grey, neutral);
	const glm::vec3 orangeOut = Graded(*gfx, orange, neutral);

	// Saturation: none leaves a colour grey, more pushes its channels apart.
	{
		auto s       = neutral;
		s.saturation = 0.0f;
		const auto c = Graded(*gfx, orange, s);
		INFO("desaturated orange: " << c.r << " " << c.g << " " << c.b);
		CHECK(Spread(c) < c_Moved);

		s.saturation = 1.5f;
		CHECK(Spread(Graded(*gfx, orange, s)) > Spread(orangeOut) + c_Moved);
	}

	// Slope and offset raise, power above one lowers -- the encoding sits in [0, 1].
	{
		auto s  = neutral;
		s.slope = glm::vec3(1.1f);
		CHECK(Luma(Graded(*gfx, grey, s)) > Luma(greyOut) + c_Moved);

		s        = neutral;
		s.offset = glm::vec3(0.03f);
		CHECK(Luma(Graded(*gfx, grey, s)) > Luma(greyOut) + c_Moved);

		s       = neutral;
		s.power = glm::vec3(1.2f);
		CHECK(Luma(Graded(*gfx, grey, s)) < Luma(greyOut) - c_Moved);
	}

	// Per channel: a red slope tints a grey red.
	{
		auto s       = neutral;
		s.slope      = glm::vec3(1.05f, 1.0f, 1.0f);
		const auto c = Graded(*gfx, grey, s);
		CHECK(c.r > c.g + c_Moved);
		CHECK(c.g == Catch::Approx(c.b).margin(c_Moved));
	}

	// Contrast pivots at middle grey: grey holds, the shadows fall and the highlights rise.
	{
		auto s     = neutral;
		s.contrast = 1.3f;

		CHECK(Graded(*gfx, grey, s).g == Catch::Approx(greyOut.g).margin(1e-4));

		const auto shadow    = glm::vec3(0.03f);
		const auto highlight = glm::vec3(2.0f);
		CHECK(Luma(Graded(*gfx, shadow, s)) < Luma(Graded(*gfx, shadow, neutral)) - c_Moved);
		CHECK(Luma(Graded(*gfx, highlight, s)) > Luma(Graded(*gfx, highlight, neutral)) + c_Moved);
	}
}

TEST_CASE(
	"White balance and the vignette move the image the way they are named",
	"[colorgrade][tonemap]")
{
	auto gfx = MakeGraphics();

	const auto grey    = glm::vec3(0.18f);
	const auto neutral = bgl::ColorGradeSettings();
	const auto greyOut = Graded(*gfx, grey, neutral);

	// Temperature: positive is warmer -- red over blue -- and negative the reverse.
	{
		auto s        = neutral;
		s.temperature = 50.0f;
		const auto w  = Graded(*gfx, grey, s);
		INFO("warm grey: " << w.r << " " << w.g << " " << w.b);
		CHECK(w.r > w.b + c_Moved);

		s.temperature = -50.0f;
		const auto c  = Graded(*gfx, grey, s);
		INFO("cool grey: " << c.r << " " << c.g << " " << c.b);
		CHECK(c.b > c.r + c_Moved);
	}

	// Tint: positive is magenta -- green under red and blue -- and negative is green.
	{
		auto s       = neutral;
		s.tint       = 50.0f;
		const auto m = Graded(*gfx, grey, s);
		INFO("magenta grey: " << m.r << " " << m.g << " " << m.b);
		CHECK(m.g < glm::min(m.r, m.b) - c_Moved);

		s.tint       = -50.0f;
		const auto g = Graded(*gfx, grey, s);
		CHECK(g.g > glm::max(g.r, g.b) + c_Moved);
	}

	// The vignette darkens toward a corner and leaves the centre alone.
	{
		auto s              = neutral;
		s.vignetteIntensity = 0.3f;

		const auto centre = glm::vec3(bgl::test::RunGradedAgX(*gfx, grey, c_Centre, s));
		const auto corner = glm::vec3(bgl::test::RunGradedAgX(*gfx, grey, glm::vec2(0.1f), s));

		CHECK(centre.g == Catch::Approx(greyOut.g).margin(1e-5));
		CHECK(Luma(corner) < Luma(centre) - c_Moved);

		// Smoother spreads the same darkening further in, so a mid-way point loses more.
		const auto midway    = glm::vec2(0.3f);
		const auto sharp     = glm::vec3(bgl::test::RunGradedAgX(*gfx, grey, midway, s));
		s.vignetteSmoothness = 1.0f;
		const auto smooth    = glm::vec3(bgl::test::RunGradedAgX(*gfx, grey, midway, s));
		CHECK(Luma(smooth) < Luma(sharp) - c_Moved);
	}
}

TEST_CASE("A neutral grade renders the ungraded image", "[colorgrade][render]")
{
	auto cube = OrangeCube();

	const std::string offPath = "assets/golden/colorgrade_off.got.png";
	const std::string onPath  = "assets/golden/colorgrade_neutral.got.png";

	cube.Capture(offPath);
	REQUIRE(OrangeCube::CentreProbe(offPath).Luma() > 0.1f);

	cube.target->SetColorGradeEnabled(true);
	cube.Capture(onPath);

	const float delta = bgl::test::MaxChannelDelta(offPath, onPath);
	INFO("largest channel difference: " << delta * 255.0f << "/255");
	CHECK(delta <= 1.0f / 255.0f);

	std::remove(offPath.c_str());
	std::remove(onPath.c_str());
}

TEST_CASE("The target's grade reaches the frame and its toggle removes it", "[colorgrade][render]")
{
	auto cube = OrangeCube();

	const std::string plainPath    = "assets/golden/colorgrade_plain.got.png";
	const std::string greyPath     = "assets/golden/colorgrade_grey.got.png";
	const std::string disabledPath = "assets/golden/colorgrade_disabled.got.png";

	cube.Capture(plainPath);
	const auto plain = OrangeCube::CentreProbe(plainPath);
	INFO("plain centre: " << plain.r << " " << plain.g << " " << plain.b);
	REQUIRE(plain.r > plain.b + 0.1f);

	auto settings       = bgl::ColorGradeSettings();
	settings.saturation = 0.0f;
	cube.target->SetColorGradeSettings(settings);
	cube.target->SetColorGradeEnabled(true);
	CHECK(cube.target->IsColorGradeEnabled());

	cube.Capture(greyPath);
	const auto grey = OrangeCube::CentreProbe(greyPath);
	INFO("desaturated centre: " << grey.r << " " << grey.g << " " << grey.b);
	CHECK(grey.r == Catch::Approx(grey.b).margin(0.02));
	CHECK(grey.r == Catch::Approx(grey.g).margin(0.02));

	cube.target->SetColorGradeEnabled(false);
	cube.Capture(disabledPath);
	CHECK(bgl::test::MaxChannelDelta(plainPath, disabledPath) <= 1.0f / 255.0f);

	std::remove(plainPath.c_str());
	std::remove(greyPath.c_str());
	std::remove(disabledPath.c_str());
}
