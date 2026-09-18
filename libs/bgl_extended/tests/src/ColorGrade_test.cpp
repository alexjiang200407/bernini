#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <limits>

// The colour grade: its settings are validated where every target setting is.

namespace
{
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
