#include "util/GoldenImage.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/error.h>
#include <bgl/types/Camera.h>
#include <bgl/types/ColorSplitSettings.h>
#include <bgl/types/PostProcess.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <core/glm.h>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <variant>

// The colour split: its settings are validated where every target setting is, and red and blue
// land where the settings say. A white Unlit cube on black is a step in every channel at each
// silhouette edge, so a channel's coverage of a box across the edge is where its step sits. At 270
// lines a setting of 8 is one output pixel, and a whole-pixel displacement keeps every pixel black
// or white, so the positions are exact.

namespace
{
	constexpr int   c_Size       = 270;
	constexpr float c_PixelUnits = 2160.0f / static_cast<float>(c_Size);

	// Camera at z = 4 looking at a unit-half-extent cube, fov 60 deg vertical: the front face's
	// edges land 77.94 px either side of the centre, at 57.06 and 212.94.
	constexpr int c_NearEdge = 57;
	constexpr int c_FarEdge  = 213;

	constexpr int c_Across = 24;
	constexpr int c_Along  = 32;
	constexpr int c_Middle = c_Size / 2 - c_Along / 2;

	bgl::test::GraphicsSetup
	Options()
	{
		auto opts                       = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir  = bgl::test::ShaderCacheDir();
		opts.gpuContext.clientShaderDir = "./shaders/tests/surfaces";
		return opts;
	}

	/** How far red and blue sit from green across one edge, in pixels, +x right and +y down. */
	struct Shift
	{
		float red  = 0.0f;
		float blue = 0.0f;
	};

	/**
	 * The step runs black to white as the coordinate grows at a near edge and white to black at a
	 * far one, so a channel's mean over the box is the share of it past, or short of, its step.
	 */
	Shift
	ShiftOf(const bgl::test::Rgba& coverage, bool nearEdge)
	{
		const float sign = nearEdge ? 1.0f : -1.0f;
		return { sign * (coverage.g - coverage.r) * static_cast<float>(c_Across),
			     sign * (coverage.g - coverage.b) * static_cast<float>(c_Across) };
	}

	Shift
	LeftEdge(const std::string& path)
	{
		return ShiftOf(
			bgl::test::MeanColor(path, c_NearEdge - c_Across / 2, c_Middle, c_Across, c_Along),
			true);
	}

	Shift
	RightEdge(const std::string& path)
	{
		return ShiftOf(
			bgl::test::MeanColor(path, c_FarEdge - c_Across / 2, c_Middle, c_Across, c_Along),
			false);
	}

	Shift
	TopEdge(const std::string& path)
	{
		return ShiftOf(
			bgl::test::MeanColor(path, c_Middle, c_NearEdge - c_Across / 2, c_Along, c_Across),
			true);
	}

	struct WhiteCube
	{
		bgl::GraphicsRef     gfx = bgl::test::CreateGraphics(Options());
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
		bgl::RenderTargetRef target;
		bgl::RenderJob       job;

		explicit WhiteCube(bgl::RenderTargetDesc targetDesc = bgl::RenderTargetDesc())
		{
			REQUIRE(gfx != nullptr);

			targetDesc.width    = c_Size;
			targetDesc.height   = c_Size;
			targetDesc.headless = true;
			target              = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);

			scene = gfx->CreateScene(bgl::SceneDesc());
			view  = gfx->CreateSceneView(scene, 4);

			const auto white = scene->CreateSurfaceMaterial(
				bgl::SurfaceMaterialDesc{
					.surfaceName = "Unlit",
					.values      = { { "color", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f) },
			                         { "opacity", glm::vec4(1.0f) } } });
			view->CreateStaticMeshInstance(
				bgl::StaticMeshInstanceDesc().SetGeom(scene->AddCubeGeom(white)));

			job.view     = view;
			job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));
			job.camera   = bgl::Camera()
			                   .LookAt(
								   glm::vec3(0.0f, 0.0f, 4.0f),
								   glm::vec3(0.0f),
								   glm::vec3(0.0f, 1.0f, 0.0f))
			                   .Perspective(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);
		}

		void
		Capture(const std::string& path, int frames = 2)
		{
			for (int i = 0; i < frames; ++i) gfx->DrawFrame(target, job);
			gfx->ScreenshotPng(target, path);
		}

		/** The target's post-process, whichever type it is, with `split` in place of its own. */
		void
		SetSplit(const std::optional<bgl::ColorSplitSettings>& split)
		{
			bgl::PostProcess postProcess = target->GetPostProcess();
			std::visit([&](auto& p) { p.split = split; }, postProcess);
			target->SetPostProcess(postProcess);
		}

		void
		Split(glm::vec2 offsetPx, float radialPx)
		{
			SetSplit(
				bgl::ColorSplitSettings{ .offset = offsetPx * c_PixelUnits,
			                             .radial = radialPx * c_PixelUnits });
		}
	};

	bgl::RenderTargetDesc
	Toon()
	{
		auto desc        = bgl::RenderTargetDesc();
		desc.postProcess = bgl::ToonPostProcess();
		return desc;
	}
}

TEST_CASE("Colour split settings that are not finite are refused", "[colorsplit]")
{
	auto cube   = WhiteCube(Toon());
	auto target = cube.target;

	const auto split = [&]() {
		return std::get<bgl::ToonPostProcess>(target->GetPostProcess()).split;
	};
	CHECK(!split());

	auto kept   = bgl::ColorSplitSettings();
	kept.offset = glm::vec2(3.0f, -1.0f);
	kept.radial = 2.0f;
	cube.SetSplit(kept);

	constexpr float c_Nan = std::numeric_limits<float>::quiet_NaN();
	constexpr float c_Inf = std::numeric_limits<float>::infinity();

	const auto refuses = [&](auto mutate) {
		auto bad = bgl::ColorSplitSettings();
		mutate(bad);
		CHECK_THROWS_AS(cube.SetSplit(bad), bgl::GraphicsError);
		CHECK_THROWS_AS(
			target->SetPostProcess(bgl::FilmicPostProcess{ .split = bad }),
			bgl::GraphicsError);
	};

	refuses([](auto& s) { s.offset.x = c_Nan; });
	refuses([](auto& s) { s.offset.y = c_Inf; });
	refuses([](auto& s) { s.radial = -c_Inf; });
	refuses([](auto& s) { s.radial = c_Nan; });

	// A refused post-process leaves the one the target had.
	REQUIRE(split());
	CHECK(split()->offset == glm::vec2(3.0f, -1.0f));
	CHECK(split()->radial == 2.0f);

	// Either sign, and no split at all, are settings rather than errors.
	auto none   = bgl::ColorSplitSettings();
	none.offset = glm::vec2(0.0f);
	none.radial = -4.0f;
	CHECK_NOTHROW(cube.SetSplit(none));
	CHECK(split()->radial == -4.0f);
}

TEST_CASE(
	"A colour split displaces red by its offset and blue by the opposite",
	"[colorsplit][render]")
{
	auto cube = WhiteCube(Toon());

	const std::string plain    = "assets/golden/colorsplit_plain.got.png";
	const std::string zero     = "assets/golden/colorsplit_zero.got.png";
	const std::string right    = "assets/golden/colorsplit_right.got.png";
	const std::string up       = "assets/golden/colorsplit_up.got.png";
	const std::string disabled = "assets/golden/colorsplit_disabled.got.png";

	cube.Capture(plain);

	// The probes sit across real edges: a channel covers half of each box, give or take the edge's
	// place in it, and none is displaced.
	const auto leftCoverage =
		bgl::test::MeanColor(plain, c_NearEdge - c_Across / 2, c_Middle, c_Across, c_Along);
	REQUIRE(leftCoverage.g > 0.4f);
	REQUIRE(leftCoverage.g < 0.6f);
	CHECK(LeftEdge(plain).red == 0.0f);
	CHECK(TopEdge(plain).blue == 0.0f);

	// Enabled with nothing to displace is the plain frame, through the linear tap.
	cube.Split(glm::vec2(0.0f), 0.0f);
	cube.Capture(zero);
	CHECK(bgl::test::MaxChannelDelta(plain, zero) <= 1.0f / 255.0f);

	constexpr float c_Exact = 0.05f;

	// Two pixels right: across a vertical edge red sits two right of green and blue two left, and
	// along a horizontal one nothing moves.
	cube.Split(glm::vec2(2.0f, 0.0f), 0.0f);
	cube.Capture(right);
	{
		const Shift left = LeftEdge(right);
		INFO("left edge: red " << left.red << ", blue " << left.blue);
		CHECK(left.red == Catch::Approx(2.0f).margin(c_Exact));
		CHECK(left.blue == Catch::Approx(-2.0f).margin(c_Exact));

		const Shift far = RightEdge(right);
		CHECK(far.red == Catch::Approx(2.0f).margin(c_Exact));
		CHECK(far.blue == Catch::Approx(-2.0f).margin(c_Exact));

		const Shift top = TopEdge(right);
		CHECK(top.red == Catch::Approx(0.0f).margin(c_Exact));
		CHECK(top.blue == Catch::Approx(0.0f).margin(c_Exact));

		// Inside the silhouette every channel still finds white.
		const auto centre = bgl::test::MeanColor(right, c_Size / 2 - 8, c_Size / 2 - 8, 16, 16);
		CHECK(centre.r == 1.0f);
		CHECK(centre.b == 1.0f);
	}

	// One pixel up, which is negative y.
	cube.Split(glm::vec2(0.0f, -1.0f), 0.0f);
	cube.Capture(up);
	{
		const Shift top = TopEdge(up);
		INFO("top edge: red " << top.red << ", blue " << top.blue);
		CHECK(top.red == Catch::Approx(-1.0f).margin(c_Exact));
		CHECK(top.blue == Catch::Approx(1.0f).margin(c_Exact));

		const Shift left = LeftEdge(up);
		CHECK(left.red == Catch::Approx(0.0f).margin(c_Exact));
	}

	cube.SetSplit(std::nullopt);
	cube.Capture(disabled);
	CHECK(bgl::test::MaxChannelDelta(plain, disabled) == 0.0f);

	for (const auto& path : { plain, zero, right, up, disabled }) std::remove(path.c_str());
}

// The edges sit 0.58 of the half-height from the centre, so two pixels of radial share is a little
// over one there -- a fraction of a pixel, which the encoded mean overstates. Direction is what
// the probes can say.
TEST_CASE(
	"A colour split's radial share points away from the centre on every side",
	"[colorsplit][render]")
{
	auto cube = WhiteCube(Toon());

	const std::string outward = "assets/golden/colorsplit_outward.got.png";
	const std::string inward  = "assets/golden/colorsplit_inward.got.png";

	constexpr float c_Moved = 0.5f;

	cube.Split(glm::vec2(0.0f), 2.0f);
	cube.Capture(outward);
	{
		const Shift left  = LeftEdge(outward);
		const Shift right = RightEdge(outward);
		const Shift top   = TopEdge(outward);
		INFO("red: left " << left.red << ", right " << right.red << ", top " << top.red);

		CHECK(left.red < -c_Moved);
		CHECK(left.blue > c_Moved);
		CHECK(right.red > c_Moved);
		CHECK(right.blue < -c_Moved);
		CHECK(top.red < -c_Moved);
		CHECK(top.blue > c_Moved);
	}

	cube.Split(glm::vec2(0.0f), -2.0f);
	cube.Capture(inward);
	CHECK(LeftEdge(inward).red > c_Moved);
	CHECK(RightEdge(inward).red < -c_Moved);

	std::remove(outward.c_str());
	std::remove(inward.c_str());
}

TEST_CASE("A colour split applies under filmic, and through the sharpen", "[colorsplit][render]")
{
	// Under filmic the split is ahead of AgX, which mixes channels, so the fringes are read by
	// their hue: left of a left edge only blue arrives, and just inside it red has not yet.
	SECTION("filmic")
	{
		auto cube = WhiteCube();
		cube.Split(glm::vec2(2.0f, 0.0f), 0.0f);

		const std::string path = "assets/golden/colorsplit_filmic.got.png";
		cube.Capture(path);

		const auto outside = bgl::test::MeanColor(path, c_NearEdge - 2, c_Middle, 2, c_Along);
		const auto inside  = bgl::test::MeanColor(path, c_NearEdge, c_Middle, 2, c_Along);
		INFO("outside " << outside.r << " " << outside.g << " " << outside.b);
		INFO("inside " << inside.r << " " << inside.g << " " << inside.b);

		CHECK(outside.b > outside.r + 0.1f);
		CHECK(outside.b > outside.g + 0.1f);
		CHECK(inside.g > inside.r + 0.1f);

		std::remove(path.c_str());
	}

	// Upscaled and resolved, RCAS runs on each displaced tap. The resolve softens the edge, so the
	// distance is approximate.
	SECTION("sharpened")
	{
		auto desc        = Toon();
		desc.taaEnabled  = true;
		desc.renderScale = 0.5f;

		auto cube = WhiteCube(desc);
		cube.Split(glm::vec2(2.0f, 0.0f), 0.0f);

		const std::string path = "assets/golden/colorsplit_sharpened.got.png";
		cube.Capture(path, 16);

		const Shift left = LeftEdge(path);
		INFO("left edge: red " << left.red << ", blue " << left.blue);
		CHECK(left.red > 1.0f);
		CHECK(left.red < 3.0f);
		CHECK(left.blue < -1.0f);
		CHECK(left.blue > -3.0f);

		std::remove(path.c_str());
	}
}
