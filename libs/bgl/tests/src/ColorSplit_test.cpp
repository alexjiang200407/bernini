#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/error.h>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <limits>

// The colour split: its settings are validated where every target setting is.

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

TEST_CASE("Colour split settings that are not finite are refused", "[colorsplit]")
{
	auto gfx    = MakeGraphics();
	auto target = MakeTarget(*gfx, 64);

	CHECK(!target->IsColorSplitEnabled());

	auto kept   = bgl::ColorSplitSettings();
	kept.offset = glm::vec2(3.0f, -1.0f);
	kept.radial = 2.0f;
	target->SetColorSplitSettings(kept);

	constexpr float c_Nan = std::numeric_limits<float>::quiet_NaN();
	constexpr float c_Inf = std::numeric_limits<float>::infinity();

	const auto refuses = [&](auto mutate) {
		auto bad = bgl::ColorSplitSettings();
		mutate(bad);
		CHECK_THROWS_AS(target->SetColorSplitSettings(bad), bgl::GraphicsError);
	};

	refuses([](auto& s) { s.offset.x = c_Nan; });
	refuses([](auto& s) { s.offset.y = c_Inf; });
	refuses([](auto& s) { s.radial = -c_Inf; });
	refuses([](auto& s) { s.radial = c_Nan; });

	// A refused set leaves the settings the target had.
	const auto got = target->GetColorSplitSettings();
	CHECK(got.offset == glm::vec2(3.0f, -1.0f));
	CHECK(got.radial == 2.0f);

	// Either sign, and no split at all, are settings rather than errors.
	auto none   = bgl::ColorSplitSettings();
	none.offset = glm::vec2(0.0f);
	none.radial = -4.0f;
	CHECK_NOTHROW(target->SetColorSplitSettings(none));

	// The toggle is its own state: turning it on keeps the settings.
	target->SetColorSplitEnabled(true);
	CHECK(target->IsColorSplitEnabled());
	CHECK(target->GetColorSplitSettings().radial == -4.0f);
}
