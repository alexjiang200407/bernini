#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/error.h>
#include <catch2/catch_test_macros.hpp>
#include <limits>

// Film grain: its settings are validated where every target setting is.

namespace
{
	bgl::GraphicsRef
	MakeGraphics()
	{
		auto opts                      = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir = bgl::test::ShaderCacheDir();

		auto gfx = bgl::test::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);
		return gfx;
	}

	bgl::RenderTargetRef
	MakeTarget(bgl::IGraphics& gfx, int size)
	{
		auto targetDesc     = bgl::RenderTargetDesc();
		targetDesc.width    = size;
		targetDesc.height   = size;
		targetDesc.headless = true;

		auto target = gfx.CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);
		return target;
	}
}

TEST_CASE("Film grain settings outside their documented ranges are refused", "[filmgrain]")
{
	auto gfx    = MakeGraphics();
	auto target = MakeTarget(*gfx, 64);

	CHECK(!target->IsFilmGrainEnabled());

	auto kept       = bgl::FilmGrainSettings();
	kept.intensity  = 0.5f;
	kept.size       = 3.0f;
	kept.holdFrames = 4;
	target->SetFilmGrainSettings(kept);

	constexpr float c_Nan = std::numeric_limits<float>::quiet_NaN();
	constexpr float c_Inf = std::numeric_limits<float>::infinity();

	const auto refuses = [&](auto mutate) {
		auto bad = bgl::FilmGrainSettings();
		mutate(bad);
		CHECK_THROWS_AS(target->SetFilmGrainSettings(bad), bgl::GraphicsError);
	};

	refuses([](auto& s) { s.intensity = -0.01f; });
	refuses([](auto& s) { s.intensity = 1.01f; });
	refuses([](auto& s) { s.intensity = c_Nan; });
	refuses([](auto& s) { s.size = 0.0f; });
	refuses([](auto& s) { s.size = -1.0f; });
	refuses([](auto& s) { s.size = c_Inf; });
	refuses([](auto& s) { s.size = c_Nan; });

	// A refused set leaves the settings the target had.
	const auto got = target->GetFilmGrainSettings();
	CHECK(got.intensity == 0.5f);
	CHECK(got.size == 3.0f);
	CHECK(got.holdFrames == 4);

	// The edges of every range are in it, and a pattern may be held forever.
	auto edges       = bgl::FilmGrainSettings();
	edges.intensity  = 0.0f;
	edges.holdFrames = 0;
	CHECK_NOTHROW(target->SetFilmGrainSettings(edges));
	edges.intensity = 1.0f;
	CHECK_NOTHROW(target->SetFilmGrainSettings(edges));

	// The toggle is its own state: turning it on keeps the settings.
	target->SetFilmGrainEnabled(true);
	CHECK(target->IsFilmGrainEnabled());
	CHECK(target->GetFilmGrainSettings().intensity == 1.0f);
}
