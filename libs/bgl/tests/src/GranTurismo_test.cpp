#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/TonemapProbe.h"
#include <array>
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>

// The stylized curve is Gran Turismo's (Uchimura 2017) with its published parameters: P = 1,
// a = 1, m = 0.22, l = 0.4, c = 1.33, b = 0. Its linear section is the identity from m to
// m + l0, l0 = (P - m) * l / a = 0.312, so a value authored there is shown as it was authored.
// The numbers below are the curve evaluated by hand in double precision.

namespace
{
	constexpr float c_LinearStart = 0.22f;
	constexpr float c_LinearEnd   = 0.532f;

	bgl::GraphicsRef
	MakeGraphics()
	{
		auto opts                        = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer = true;

		auto gfx = bgl::test::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);
		return gfx;
	}
}

TEST_CASE("Gran Turismo's curve is the identity on its linear section", "[tonemap][granturismo]")
{
	auto gfx = MakeGraphics();

	constexpr std::array<float, 5> c_Linear = { { c_LinearStart, 0.3f, 0.4f, 0.5f, c_LinearEnd } };

	for (const float x : c_Linear)
	{
		const float y = bgl::test::RunGranTurismo(*gfx, x);
		INFO("GT(" << x << ") = " << y);
		CHECK(y == Catch::Approx(x).margin(1e-6));
	}

	// Black is black: the pedestal is zero.
	CHECK(bgl::test::RunGranTurismo(*gfx, 0.0f) == Catch::Approx(0.0f).margin(1e-7));
}

// The values a hand evaluation of Uchimura's formula gives, one in each section: the toe, which
// blends into the linear section by m, and the exponential shoulder past m + l0.
TEST_CASE(
	"Gran Turismo's curve's toe and shoulder sit where the formula puts them",
	"[tonemap][granturismo]")
{
	auto gfx = MakeGraphics();

	struct Point
	{
		float x;
		float y;
	};

	constexpr std::array<Point, 6> c_Points = { {
		{ 0.01f, 0.003644f },
		{ 0.1f, 0.086988f },
		{ 0.18f, 0.178995f },
		{ 0.6f, 0.595291f },
		{ 1.0f, 0.827832f },
		{ 2.0f, 0.979678f },
	} };

	for (const Point& p : c_Points)
	{
		const float y = bgl::test::RunGranTurismo(*gfx, p.x);
		INFO("GT(" << p.x << ") = " << y << ", formula " << p.y);
		CHECK(y == Catch::Approx(p.y).margin(1e-5));
	}

	// The toe lies under the identity, so a shadow is deepened rather than lifted.
	for (const float x : { 0.01f, 0.05f, 0.1f, 0.18f })
	{
		CHECK(bgl::test::RunGranTurismo(*gfx, x) < x);
	}
}

// Rising everywhere, and bounded by P: a highlight is rolled off toward white and never past it,
// however bright the sun or the bloom makes it.
TEST_CASE("Gran Turismo's curve rises to white and never past it", "[tonemap][granturismo]")
{
	auto gfx = MakeGraphics();

	constexpr std::array<float, 14> c_Sweep = {
		{ 0.001f, 0.01f, 0.05f, 0.1f, 0.18f, 0.25f, 0.5f, 0.7f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f }
	};

	float previous = 0.0f;
	for (const float x : c_Sweep)
	{
		const float y = bgl::test::RunGranTurismo(*gfx, x);
		INFO("GT(" << x << ") = " << y);
		CHECK(y > previous);
		CHECK(y < 1.0f);
		previous = y;
	}

	for (const float x : { 16.0f, 1000.0f, 1.0e6f })
	{
		const float y = bgl::test::RunGranTurismo(*gfx, x);
		INFO("GT(" << x << ") = " << y);
		CHECK(y <= 1.0f);
		CHECK(y > 0.9999f);
	}
}

// Per channel: a colour's channels are each the grey curve's value, so a channel is never mixed
// into another and a hue the linear section holds is shown as authored.
TEST_CASE("Gran Turismo's curve maps each channel alone", "[tonemap][granturismo]")
{
	auto gfx = MakeGraphics();

	const auto      colour = glm::vec3(0.45f, 0.25f, 1.2f);
	const glm::vec3 out    = bgl::test::RunGranTurismo(*gfx, colour);

	CHECK(out.r == Catch::Approx(bgl::test::RunGranTurismo(*gfx, colour.r)).margin(1e-6));
	CHECK(out.g == Catch::Approx(bgl::test::RunGranTurismo(*gfx, colour.g)).margin(1e-6));
	CHECK(out.b == Catch::Approx(bgl::test::RunGranTurismo(*gfx, colour.b)).margin(1e-6));
	CHECK(out.r == Catch::Approx(0.45f).margin(1e-6));
	CHECK(out.g == Catch::Approx(0.25f).margin(1e-6));
}
