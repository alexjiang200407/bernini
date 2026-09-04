#include "Windows/AnimationEditor/foot_ik_weights.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// The Animation panel's IK weight sliders, lifted clear of the window: what a slider commits is a
// constant record on every leg, because the panel's clock wraps and a ramp in it would never hold.

TEST_CASE("A slider commits a constant weight on every leg", "[animation][footik]")
{
	const bgl::FootIKDesc desc = editor::FootIKForSliders(50, 25);
	for (const bgl::FootIKLegDesc& leg : desc.leg)
	{
		CHECK(leg.position.from == Catch::Approx(0.5f));
		CHECK(leg.position.to == Catch::Approx(0.5f));
		CHECK(leg.rotation.from == Catch::Approx(0.25f));
		CHECK(leg.rotation.to == Catch::Approx(0.25f));

		// No window at all: a ramp whose end is its start is a step, and one at t = 0 is a
		// constant under any clock, wrapped or not.
		CHECK(leg.position.start == 0.0f);
		CHECK(leg.position.end == 0.0f);
	}
}

TEST_CASE("Full sliders are the default record", "[animation][footik]")
{
	const bgl::FootIKDesc desc  = editor::FootIKForSliders(100, 100);
	const auto            fresh = bgl::FootIKDesc();
	const auto            same  = [](const bgl::WeightRamp& a, const bgl::WeightRamp& b) {
		CHECK(a.from == b.from);
		CHECK(a.to == b.to);
		CHECK(a.start == b.start);
		CHECK(a.end == b.end);
	};
	for (size_t i = 0; i < desc.leg.size(); ++i)
	{
		same(desc.leg[i].position, fresh.leg[i].position);
		same(desc.leg[i].rotation, fresh.leg[i].rotation);
	}
}

TEST_CASE("A slider past its range is clamped into it", "[animation][footik]")
{
	const bgl::FootIKDesc high = editor::FootIKForSliders(140, -5);
	CHECK(high.leg[0].position.to == 1.0f);
	CHECK(high.leg[0].rotation.to == 0.0f);
}
