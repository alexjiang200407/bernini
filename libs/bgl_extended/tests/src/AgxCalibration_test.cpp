#include "util/AgxProbe.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>

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

/**
 * Middle grey comes out where Blender's AgX puts it.
 *
 * This is the anchor every comparison against another renderer is made through, and it is exactly
 * the kind of constant that drifts silently: the polynomial, the log range and the two matrices can
 * each be edited into something that still looks like a tone map. Scene-linear 0.18 landing at
 * display 0.5 is the one number that says they are all still right together.
 *
 * Also catches the commonest way to break AgX -- dropping the closing linearization, which leaves
 * the sRGB target encoding an already-encoded value and washes the whole image out. That mistake
 * moves this to about 0.73.
 */
TEST_CASE("AgX places middle grey at display 0.5", "[tonemap][agx][calibration]")
{
	auto gfx = MakeGraphics();

	const glm::vec4 result = bgl::test::RunAgX(*gfx, 0.18f);

	// Grey in, grey out: the inset/outset pair is its own inverse on the neutral axis, so a
	// transposed matrix shows up here as a tint before it shows up as a level.
	CHECK(result.r == Catch::Approx(result.g).margin(0.005));
	CHECK(result.g == Catch::Approx(result.b).margin(0.005));

	const float display = bgl::test::EncodeSrgb(result.r);
	INFO("AgX(0.18) = " << result.r << " linear, " << display << " display");
	CHECK(display == Catch::Approx(0.5f).margin(0.01));
}

// The curve still has to be a curve. A tone map that had collapsed to a constant would satisfy the
// anchor above and nothing else, and flatness is the symptom this whole area is being measured for.
TEST_CASE("AgX keeps its range monotonic around middle grey", "[tonemap][agx][calibration]")
{
	auto gfx = MakeGraphics();

	const float dark = bgl::test::EncodeSrgb(bgl::test::RunAgX(*gfx, 0.045f).r);  // two stops under
	const float middle = bgl::test::EncodeSrgb(bgl::test::RunAgX(*gfx, 0.18f).r);
	const float bright = bgl::test::EncodeSrgb(bgl::test::RunAgX(*gfx, 0.72f).r);  // two stops over

	INFO("dark " << dark << ", middle " << middle << ", bright " << bright);

	CHECK(dark < middle);
	CHECK(middle < bright);

	// Two stops either side of grey must still be plainly apart after the compression, or a
	// four-stop environment would read as one flat tone.
	CHECK(middle - dark > 0.1f);
	CHECK(bright - middle > 0.1f);
}
