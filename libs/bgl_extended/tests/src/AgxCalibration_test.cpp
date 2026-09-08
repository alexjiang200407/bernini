#include "util/AgxProbe.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>

// The tone map is Blender 5.2's AgX, and the numbers here are Blender's: a grey emission at each
// scene-linear value rendered through its factory view (AgX, no look, exposure 0), read off the
// PNG it wrote. Re-measure them with scripts/blender_probe.py's sweep when the LUT is regenerated.
//
// A sixth-order fit of the older AgX sat 0.02-0.05 above these through the whole midtone range and
// pinned 0.18 at 0.5 as Blender's anchor. Blender pins 0.18 where sRGB does, at 0.461, and this is
// the one place that says so.

namespace
{
	struct Point
	{
		float sceneLinear;
		float blenderDisplay;
	};

	constexpr std::array<Point, 12> c_BlenderSweep = { {
		{ 0.01f, 0.0725f },
		{ 0.045f, 0.2127f },
		{ 0.1f, 0.3403f },
		{ 0.18f, 0.4612f },
		{ 0.3f, 0.5700f },
		{ 0.5f, 0.6652f },
		{ 0.72f, 0.7242f },
		{ 1.0f, 0.7710f },
		{ 2.0f, 0.8519f },
		{ 4.0f, 0.9137f },
		{ 8.0f, 0.9617f },
		{ 16.0f, 0.9986f },
	} };

	// Blender reads its LUT tetrahedrally and the strip reads it trilinearly; over a 57-point log
	// axis the two disagree by less than this. An exposure or a lost linearization is ten times it.
	constexpr float c_Margin = 0.006f;

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

/**
 * Middle grey comes out where Blender's AgX puts it.
 *
 * This is the anchor every comparison against another renderer is made through, and it is exactly
 * the kind of constant that drifts silently: the matrix, the log range and the decode can each be
 * edited into something that still looks like a tone map. Scene-linear 0.18 landing at Blender's
 * 0.461 is the one number that says they are all still right together.
 *
 * Also catches the commonest way to break it -- dropping the closing linearization, which leaves the
 * sRGB target encoding an already-encoded value and washes the whole image out.
 */
TEST_CASE("AgX places middle grey where Blender does", "[tonemap][agx][calibration]")
{
	auto gfx = MakeGraphics();

	const glm::vec4 result = bgl::test::RunAgX(*gfx, 0.18f);

	// Grey in, grey out: the LUT's neutral axis is neutral, so a transposed matrix or a swapped
	// strip axis shows up here as a tint before it shows up as a level.
	CHECK(result.r == Catch::Approx(result.g).margin(0.005));
	CHECK(result.g == Catch::Approx(result.b).margin(0.005));

	const float display = bgl::test::EncodeSrgb(result.r);
	INFO("AgX(0.18) = " << result.r << " linear, " << display << " display");
	CHECK(display == Catch::Approx(0.4612f).margin(c_Margin));
}

// The whole curve, not one point: an anchor alone would pass a LUT read through the wrong axis, or
// a log range a stop out, as long as 0.18 still happened to land.
TEST_CASE("AgX follows Blender's curve from black to white", "[tonemap][agx][calibration]")
{
	auto gfx = MakeGraphics();

	float previous = -1.0f;
	for (const Point& point : c_BlenderSweep)
	{
		const float display = bgl::test::EncodeSrgb(bgl::test::RunAgX(*gfx, point.sceneLinear).r);
		INFO(
			"AgX(" << point.sceneLinear << ") = " << display << ", Blender "
				   << point.blenderDisplay);
		CHECK(display == Catch::Approx(point.blenderDisplay).margin(c_Margin));
		CHECK(display > previous);
		previous = display;
	}
}
