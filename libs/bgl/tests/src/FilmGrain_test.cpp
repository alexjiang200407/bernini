#include "util/GoldenImage.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/TonemapProbe.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/error.h>
#include <bgl/types/Camera.h>
#include <bgl/types/FilmGrainSettings.h>
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
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>

// Film grain: its settings are validated where every target setting is, and the grain on a frame
// is what they say. A full-frame Unlit plane puts one known value on every pixel, so whatever
// varies across the frame is grain and nothing else. At 128 lines the default pitch is
// under a pixel and floors at one, where the grain is white: neighbouring pixels are independent
// and AliasEnergy, their mean squared difference, is twice its variance.

namespace
{
	constexpr int c_Size = 128;

	// The grain's deviation as a share of the pixel's display-linear value, per unit of intensity:
	// the sum of two uniforms over (-1, 1).
	const float c_TriangularSigma = 1.0f / std::sqrt(6.0f);

	bgl::test::GraphicsSetup
	Options()
	{
		auto opts                       = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir  = bgl::test::ShaderCacheDir();
		opts.gpuContext.clientShaderDir = "./shaders/tests/surfaces";
		return opts;
	}

	/** The slope of the sRGB encode at `linear`, which is what turns a linear deviation into the PNG's. */
	float
	EncodeSlope(float linear)
	{
		constexpr float c_Step = 1e-3f;
		return (bgl::test::EncodeSrgb(linear + c_Step) - bgl::test::EncodeSrgb(linear - c_Step)) /
		       (2.0f * c_Step);
	}

	struct Plane
	{
		bgl::GraphicsRef     gfx = bgl::test::CreateGraphics(Options());
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
		bgl::RenderTargetRef target;
		bgl::RenderJob       job;

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

			job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));
			job.camera   = bgl::Camera()
			                   .LookAt(
								   glm::vec3(0.0f, 0.0f, 5.0f),
								   glm::vec3(0.0f),
								   glm::vec3(0.0f, 1.0f, 0.0f))
			                   .Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
		}

		/** Fills the frame with one radiance from the next frame on. */
		void
		Fill(float radiance)
		{
			const auto material = scene->CreateSurfaceMaterial(
				bgl::SurfaceMaterialDesc{
					.surfaceName = "Unlit",
					.values      = { { "color", glm::vec4(glm::vec3(radiance), 0.0f) },
			                         { "opacity", glm::vec4(1.0f) } } });
			view = gfx->CreateSceneView(scene, 4);
			view->CreateStaticMeshInstance(
				bgl::StaticMeshInstanceDesc().SetGeom(
					scene->AddPlaneGeom(1, 1, 40.0f, 40.0f, material)));
			job.view = view;

			// The first frame of a view uploads; the captures that follow each show their own.
			gfx->DrawFrame(target, job);
		}

		void
		Capture(const std::string& path, int frames = 1)
		{
			for (int i = 0; i < frames; ++i) gfx->DrawFrame(target, job);
			gfx->ScreenshotPng(target, path);
		}

		/** The target's post-process with `grain` in place of its own. */
		void
		SetGrain(const std::optional<bgl::FilmGrainSettings>& grain)
		{
			bgl::PostProcess postProcess = target->GetPostProcess();
			postProcess.grain            = grain;
			target->SetPostProcess(postProcess);
		}

		[[nodiscard]] std::optional<bgl::FilmGrainSettings>
		GetGrain() const
		{
			return target->GetPostProcess().grain;
		}

		void
		Grain(float intensity, uint32_t holdFrames = 0, float size = bgl::FilmGrainSettings().size)
		{
			SetGrain(
				bgl::FilmGrainSettings{ .intensity  = intensity,
			                            .size       = size,
			                            .holdFrames = holdFrames });
		}
	};

	/** The deviation of white grain over the whole frame, in the PNG's encoded units. */
	float
	Deviation(const std::string& path)
	{
		return std::sqrt(bgl::test::AliasEnergy(path, 0, 0, c_Size, c_Size) / 2.0f);
	}

	/** The display-linear value AgX put on a plain frame, read back off its encoding. */
	float
	Shown(const std::string& path)
	{
		return bgl::test::DecodeSrgb(bgl::test::MeanColor(path, 0, 0, c_Size, c_Size).g);
	}

	bool
	SameFrame(const std::string& a, const std::string& b)
	{
		return bgl::test::MaxChannelDelta(a, b) == 0.0f;
	}
}

TEST_CASE("Film grain settings outside their documented ranges are refused", "[filmgrain]")
{
	auto plane = Plane();

	CHECK(!plane.GetGrain());

	auto kept       = bgl::FilmGrainSettings();
	kept.intensity  = 0.5f;
	kept.size       = 3.0f;
	kept.holdFrames = 4;
	plane.SetGrain(kept);

	constexpr float c_Nan = std::numeric_limits<float>::quiet_NaN();
	constexpr float c_Inf = std::numeric_limits<float>::infinity();

	const auto refuses = [&](auto mutate) {
		auto bad = bgl::FilmGrainSettings();
		mutate(bad);
		CHECK_THROWS_AS(plane.SetGrain(bad), bgl::GraphicsError);
	};

	refuses([](auto& s) { s.intensity = -0.01f; });
	refuses([](auto& s) { s.intensity = 1.01f; });
	refuses([](auto& s) { s.intensity = c_Nan; });
	refuses([](auto& s) { s.size = 0.0f; });
	refuses([](auto& s) { s.size = -1.0f; });
	refuses([](auto& s) { s.size = c_Inf; });
	refuses([](auto& s) { s.size = c_Nan; });

	// A refused post-process leaves the one the target had.
	const auto got = plane.GetGrain();
	REQUIRE(got);
	CHECK(got->intensity == 0.5f);
	CHECK(got->size == 3.0f);
	CHECK(got->holdFrames == 4);

	// The edges of every range are in it, and a pattern may be held forever.
	auto edges       = bgl::FilmGrainSettings();
	edges.intensity  = 0.0f;
	edges.holdFrames = 0;
	CHECK_NOTHROW(plane.SetGrain(edges));
	edges.intensity = 1.0f;
	CHECK_NOTHROW(plane.SetGrain(edges));
	CHECK(plane.GetGrain()->intensity == 1.0f);
}

TEST_CASE(
	"Film grain is a share of the value it is on, and leaves black black",
	"[filmgrain][render]")
{
	auto plane = Plane();

	const std::string plain = "assets/golden/filmgrain_plain.got.png";
	const std::string grain = "assets/golden/filmgrain_on.got.png";

	constexpr float c_Intensity = 0.25f;

	// 15%: the encode is a curve, so its slope at the mean only approximates what it does to a
	// deviation this wide.
	constexpr double c_Close = 0.15;

	// Both shown low enough that twice the intensity still peaks under display white, where it
	// clips: AgX puts them near 0.2 and 0.5.
	for (const float radiance : { 0.2f, 0.72f })
	{
		INFO("scene-linear " << radiance);

		plane.SetGrain(std::nullopt);
		plane.Fill(radiance);
		plane.Capture(plain);
		REQUIRE(Deviation(plain) == 0.0f);

		const float shown = Shown(plain);
		REQUIRE(shown * (1.0f + 2.0f * c_Intensity) < 1.0f);

		plane.Grain(c_Intensity);
		plane.Capture(grain);

		const float expected = c_Intensity * shown * c_TriangularSigma * EncodeSlope(shown);
		INFO("deviation " << Deviation(grain) * 255.0f << "/255, expected " << expected * 255.0f);
		CHECK(Deviation(grain) == Catch::Approx(expected).epsilon(c_Close));

		// Grain moves pixels about the value, not the frame off it.
		const auto before = bgl::test::MeanColor(plain, 0, 0, c_Size, c_Size);
		const auto after  = bgl::test::MeanColor(grain, 0, 0, c_Size, c_Size);
		CHECK(after.g == Catch::Approx(before.g).margin(2.0 / 255.0));

		// Monochrome: every channel of a pixel moves together, so its hue does not scatter. A
		// grain per channel would part them by about the deviation itself.
		const float chroma = std::sqrt(bgl::test::ChromaEnergy(grain, 0, 0, c_Size, c_Size));
		INFO("hue scatter " << chroma * 255.0f << "/255");
		CHECK(chroma < 0.25f * Deviation(grain));

		// Twice the intensity is twice the grain.
		plane.Grain(2.0f * c_Intensity);
		plane.Capture(grain);
		CHECK(Deviation(grain) == Catch::Approx(2.0f * expected).epsilon(c_Close));
	}

	// At display white there is no headroom: the upward half clips, so the frame darkens by the
	// mean of the half that is left, intensity / 6. AgX reaches white at the top of its range.
	plane.SetGrain(std::nullopt);
	plane.Fill(10000.0f);
	plane.Capture(plain);
	REQUIRE(bgl::test::MeanColor(plain, 0, 0, c_Size, c_Size).g == 1.0f);
	plane.Grain(c_Intensity);
	plane.Capture(grain);
	{
		const float white = bgl::test::MeanColor(grain, 0, 0, c_Size, c_Size).g;
		INFO("mean of grained white: " << white);
		CHECK(
			white == Catch::Approx(1.0f - EncodeSlope(0.999f) * c_Intensity / 6.0f).margin(0.005));
	}

	// Nothing to take a share of: full-strength grain on black is black.
	plane.Fill(0.0f);
	plane.Grain(1.0f);
	plane.Capture(grain);
	CHECK(bgl::test::MeanColor(grain, 0, 0, c_Size, c_Size).g == 0.0f);
	CHECK(Deviation(grain) == 0.0f);

	// No intensity is the plain frame, and so is no grain once grain has run.
	plane.Fill(0.5f);
	plane.SetGrain(std::nullopt);
	plane.Capture(plain);
	plane.Grain(0.0f);
	plane.Capture(grain);
	CHECK(SameFrame(plain, grain));

	plane.Grain(0.5f);
	plane.Capture(grain);
	CHECK(!SameFrame(plain, grain));

	plane.SetGrain(std::nullopt);
	plane.Capture(grain);
	CHECK(SameFrame(plain, grain));

	std::remove(plain.c_str());
	std::remove(grain.c_str());
}

TEST_CASE("A grain pattern is held for its frames, and forever at zero", "[filmgrain][render]")
{
	auto plane = Plane();
	plane.Fill(0.5f);

	const auto path = [](int i) {
		return "assets/golden/filmgrain_frame_" + std::to_string(i) + ".got.png";
	};

	// Nine consecutive frames span eight steps, and a pattern held for four changes on exactly two
	// of them wherever the run starts.
	constexpr int c_Frames = 9;

	const auto changes = [&](uint32_t holdFrames) {
		plane.Grain(0.25f, holdFrames);
		for (int i = 0; i < c_Frames; ++i) plane.Capture(path(i));

		int changed = 0;
		for (int i = 0; i + 1 < c_Frames; ++i) changed += SameFrame(path(i), path(i + 1)) ? 0 : 1;
		return changed;
	};

	CHECK(changes(0) == 0);
	CHECK(changes(1) == c_Frames - 1);
	CHECK(changes(4) == 2);

	// Two patterns are unrelated: the difference between them is two grains' worth, not a shift.
	plane.Grain(0.25f, 1);
	plane.Capture(path(0));
	plane.Capture(path(1));
	const float between = bgl::test::FrameDelta(path(0), path(1), 0, 0, c_Size, c_Size);
	const float within  = bgl::test::AliasEnergy(path(0), 0, 0, c_Size, c_Size);
	INFO("between frames " << between << ", between neighbours " << within);
	CHECK(between == Catch::Approx(within).epsilon(0.2));

	for (int i = 0; i < c_Frames; ++i) std::remove(path(i).c_str());
}

TEST_CASE("Film grain's pitch scales with the output and floors at a pixel", "[filmgrain][render]")
{
	auto plane = Plane();
	plane.Fill(0.5f);

	const std::string fine   = "assets/golden/filmgrain_fine.got.png";
	const std::string floor  = "assets/golden/filmgrain_floor.got.png";
	const std::string coarse = "assets/golden/filmgrain_coarse.got.png";

	constexpr float c_OnePixel = 2160.0f / static_cast<float>(c_Size);

	plane.Grain(0.25f, 0, c_OnePixel);
	plane.Capture(fine);

	// Under a pixel there is nothing finer to draw: the same grain as at one.
	plane.Grain(0.25f, 0, 0.1f * c_OnePixel);
	plane.Capture(floor);
	CHECK(SameFrame(fine, floor));

	// At three pixels neighbours share most of their cells, so they differ far less.
	plane.Grain(0.25f, 0, 3.0f * c_OnePixel);
	plane.Capture(coarse);

	const float fineEnergy   = bgl::test::AliasEnergy(fine, 0, 0, c_Size, c_Size);
	const float coarseEnergy = bgl::test::AliasEnergy(coarse, 0, 0, c_Size, c_Size);
	INFO("neighbour energy: fine " << fineEnergy << ", coarse " << coarseEnergy);
	CHECK(coarseEnergy > 0.0f);
	CHECK(coarseEnergy < 0.25f * fineEnergy);

	std::remove(fine.c_str());
	std::remove(floor.c_str());
	std::remove(coarse.c_str());
}
