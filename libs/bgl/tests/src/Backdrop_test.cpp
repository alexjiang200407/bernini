#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/TonemapProbe.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/BackdropGradient.h>
#include <bgl/types/Camera.h>
#include <bgl/types/ColorGradeSettings.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/SkyboxDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>

// The backdrop gradient, drawn in the sky's place. A frame of empty background is the gradient's
// answer row by row, through AgX, which the suite's probe computes with the shader's own code.

namespace
{
	constexpr int c_Size = 64;

	// The rows a strip averages; thin, so the sRGB curve across it stays inside the margin.
	constexpr int c_StripRows = 4;

	// What the strip of rows starting at `y` should read: the gradient at its mean height, through
	// AgX and encoded.
	glm::vec3
	ExpectedStrip(bgl::IGraphics& gfx, const bgl::BackdropGradient& gradient, int y)
	{
		const float     centre = static_cast<float>(y) + 0.5f * static_cast<float>(c_StripRows);
		const float     height = 1.0f - centre / static_cast<float>(c_Size);
		const glm::vec3 linear = glm::mix(gradient.bottom, gradient.top, height);
		const auto      shown  = glm::vec3(
			bgl::test::RunGradedAgX(gfx, linear, glm::vec2(0.5f), bgl::ColorGradeSettings()));
		return { bgl::test::EncodeSrgb(shown.r),
			     bgl::test::EncodeSrgb(shown.g),
			     bgl::test::EncodeSrgb(shown.b) };
	}

	struct Stage
	{
		bgl::GraphicsRef     gfx = bgl::test::CreateGraphics(Options());
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
		bgl::RenderTargetRef target;
		bgl::RenderJob       job;

		static bgl::test::GraphicsSetup
		Options()
		{
			auto opts                      = bgl::test::GraphicsSetup();
			opts.gpuContext.shaderCacheDir = bgl::test::ShaderCacheDir();
			return opts;
		}

		Stage()
		{
			REQUIRE(gfx != nullptr);
			scene = gfx->CreateScene(bgl::SceneDesc());
			view  = gfx->CreateSceneView(scene, 4);

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = c_Size;
			targetDesc.height   = c_Size;
			targetDesc.headless = true;
			target              = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);

			job.view     = view;
			job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));
			Aim(glm::vec3(0.0f, 0.0f, 5.0f));
		}

		void
		Aim(const glm::vec3& eye)
		{
			job.camera = bgl::Camera()
			                 .LookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			                 .Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
		}

		void
		ShowSky(bool followsView, float rotationY = 0.0f)
		{
			bgl::test::ApplyEnvironment(scene.Get(), view.Get());
			auto sky          = bgl::SkyboxDesc();
			sky.skyboxCubeTex = bgl::test::LoadSkybox(scene.Get());
			sky.followsView   = followsView;
			sky.rotationY     = rotationY;
			view->SetSkyBox(sky);
		}

		std::string
		Shoot(const std::string& name)
		{
			const auto png =
				(std::filesystem::temp_directory_path() / ("bernini_backdrop_" + name + ".png"))
					.string();
			for (int i = 0; i < 2; ++i) gfx->DrawFrame(target, job);
			gfx->ScreenshotPng(target, png);
			return png;
		}
	};

	void
	CheckStrip(
		bgl::IGraphics&              gfx,
		const std::string&           png,
		const bgl::BackdropGradient& gradient,
		int                          y)
	{
		constexpr float c_Margin = 2.0f / 255.0f;

		INFO("rows " << y << ".." << y + c_StripRows - 1);
		const bgl::test::Rgba got      = bgl::test::MeanColor(png, 0, y, c_Size, c_StripRows);
		const glm::vec3       expected = ExpectedStrip(gfx, gradient, y);
		CHECK(got.r == Catch::Approx(expected.r).margin(c_Margin));
		CHECK(got.g == Catch::Approx(expected.g).margin(c_Margin));
		CHECK(got.b == Catch::Approx(expected.b).margin(c_Margin));
	}
}

TEST_CASE(
	"A backdrop runs from its bottom colour to its top, in place of the sky",
	"[backdrop][render]")
{
	Stage stage;
	stage.ShowSky(false);

	const auto gradient = bgl::BackdropGradient();
	stage.view->SetBackdrop(gradient);
	const std::string png = stage.Shoot("gradient");

	CheckStrip(*stage.gfx, png, gradient, 0);
	CheckStrip(*stage.gfx, png, gradient, c_Size / 2 - c_StripRows / 2);
	CheckStrip(*stage.gfx, png, gradient, c_Size - c_StripRows);

	std::filesystem::remove(png);
}

TEST_CASE("A backdrop is fixed to the screen, wherever the camera looks", "[backdrop][render]")
{
	Stage stage;
	stage.ShowSky(false);
	stage.view->SetBackdrop(bgl::BackdropGradient());

	const std::string ahead = stage.Shoot("ahead");
	stage.Aim(glm::vec3(4.0f, 3.0f, -2.0f));
	const std::string turned = stage.Shoot("turned");

	CHECK(bgl::test::MaxChannelDelta(ahead, turned) == 0.0f);

	std::filesystem::remove(ahead);
	std::filesystem::remove(turned);
}

TEST_CASE("A view with no sky draws a backdrop too", "[backdrop][render]")
{
	Stage      stage;
	const auto gradient = bgl::BackdropGradient{ .bottom = glm::vec3(0.6f, 0.1f, 0.05f),
		                                         .top    = glm::vec3(0.02f, 0.3f, 0.7f) };
	stage.view->SetBackdrop(gradient);
	const std::string png = stage.Shoot("skyless");

	CheckStrip(*stage.gfx, png, gradient, 0);
	CheckStrip(*stage.gfx, png, gradient, c_Size - c_StripRows);

	std::filesystem::remove(png);
}

TEST_CASE("Clearing the backdrop brings back the sky it covered", "[backdrop][render]")
{
	Stage stage;
	stage.ShowSky(false);

	const std::string sky = stage.Shoot("sky");
	stage.view->SetBackdrop(bgl::BackdropGradient());
	const std::string covered = stage.Shoot("covered");
	stage.view->ClearBackdrop();
	const std::string back = stage.Shoot("back");

	CHECK(bgl::test::MaxChannelDelta(sky, covered) > 0.05f);
	CHECK(bgl::test::MaxChannelDelta(sky, back) == 0.0f);

	std::filesystem::remove(sky);
	std::filesystem::remove(covered);
	std::filesystem::remove(back);
}

TEST_CASE(
	"A sky set under the backdrop stays hidden, and is the one clearing it shows",
	"[backdrop][render]")
{
	Stage stage;
	stage.ShowSky(false);

	const std::string first = stage.Shoot("first_sky");
	stage.view->SetBackdrop(bgl::BackdropGradient());
	const std::string covered = stage.Shoot("covered_first");

	REQUIRE_NOTHROW(stage.ShowSky(false, 2.0f));
	const std::string stillCovered = stage.Shoot("covered_second");
	CHECK(bgl::test::MaxChannelDelta(covered, stillCovered) == 0.0f);

	stage.view->ClearBackdrop();
	const std::string second = stage.Shoot("second_sky");
	CHECK(bgl::test::MaxChannelDelta(second, covered) > 0.05f);
	CHECK(bgl::test::MaxChannelDelta(second, first) > 0.05f);

	for (const auto& png : { first, covered, stillCovered, second }) std::filesystem::remove(png);
}

TEST_CASE(
	"A backdrop leaves the lighting of a sky that follows the view as it was",
	"[backdrop][render]")
{
	Stage stage;
	stage.ShowSky(true);
	stage.Aim(glm::vec3(3.0f, 2.0f, 4.0f));

	auto mirror            = bgl::PbrMaterialDesc();
	mirror.baseColorFactor = glm::vec4(1.0f);
	mirror.metallicFactor  = 1.0f;
	mirror.roughnessFactor = 0.2f;
	stage.view->CreateStaticMeshInstance(
		bgl::StaticMeshInstanceDesc().SetGeom(
			stage.scene->AddPlaneGeom(1, 1, 3.0f, 3.0f, stage.scene->CreatePbrMaterial(mirror))));

	constexpr int c_Box = 8;
	constexpr int c_At  = c_Size / 2 - c_Box / 2;

	const std::string lit = stage.Shoot("lit");
	stage.view->SetBackdrop(bgl::BackdropGradient());
	const std::string backed = stage.Shoot("backed");

	// The sample box is on the mirror, not the background: a miss reads the sky or the gradient.
	CHECK(bgl::test::MeanColor(lit, c_At, c_At, c_Box, c_Box).Luma() > 0.02f);
	CHECK(bgl::test::FrameDelta(lit, backed, c_At, c_At, c_Box, c_Box) == 0.0f);
	CHECK(bgl::test::MaxChannelDelta(lit, backed) > 0.05f);

	std::filesystem::remove(lit);
	std::filesystem::remove(backed);
}

TEST_CASE("A backdrop colour that is negative or not finite is refused", "[backdrop]")
{
	Stage stage;

	auto negative      = bgl::BackdropGradient();
	negative.top.g     = -0.1f;
	auto notFinite     = bgl::BackdropGradient();
	notFinite.bottom.b = std::numeric_limits<float>::quiet_NaN();
	auto infinite      = bgl::BackdropGradient();
	infinite.top.r     = std::numeric_limits<float>::infinity();

	CHECK_THROWS_AS(stage.view->SetBackdrop(negative), bgl::SceneError);
	CHECK_THROWS_AS(stage.view->SetBackdrop(notFinite), bgl::SceneError);
	CHECK_THROWS_AS(stage.view->SetBackdrop(infinite), bgl::SceneError);
	CHECK_NOTHROW(stage.view->ClearBackdrop());
}
