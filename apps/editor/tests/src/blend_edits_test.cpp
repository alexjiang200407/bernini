#include "Windows/AnimationEditor/blend_edits.h"

#include <assetlib/blend.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>
#include <vector>

// The rules a blend space's sample run obeys while it is authored, lifted clear of the panel that
// drives them. What every case here is really checking is one property: whatever the gesture, the
// run that comes out is one `validateBlendSet` and `AddRig` would accept -- two or more samples,
// finite parameters, strictly increasing. A break caught here is a control that will not move; the
// same break caught at the door is a scene that will not load.

namespace
{
	constexpr float c_Step = 0.01f;  // two decimals, the step the panel's box shows

	std::vector<assetlib::BlendSpaceSample>
	Run()
	{
		return { { "walk", 1.5f }, { "jog", 3.2f }, { "run", 6.0f } };
	}
}

TEST_CASE("A sample lands in parameter order", "[animation][blend]")
{
	const std::vector<assetlib::BlendSpaceSample> run = Run();

	CHECK(editor::InsertionIndex(run, 0.0f) == 0);
	CHECK(editor::InsertionIndex(run, 2.0f) == 1);
	CHECK(editor::InsertionIndex(run, 4.0f) == 2);
	CHECK(editor::InsertionIndex(run, 9.0f) == 3);

	SECTION("an empty run takes the first sample at the front")
	{
		CHECK(editor::InsertionIndex({}, 4.0f) == 0);
	}
}

TEST_CASE("A sample may not land on one already there", "[animation][blend]")
{
	const std::vector<assetlib::BlendSpaceSample> run = Run();

	SECTION("clear of every neighbour is accepted")
	{
		CHECK(editor::CanInsertAt(run, 0.0f, c_Step));
		CHECK(editor::CanInsertAt(run, 2.35f, c_Step));
		CHECK(editor::CanInsertAt(run, 12.0f, c_Step));
	}

	SECTION("exactly on a sample is the duplicate validateBlendSet refuses")
	{
		CHECK_FALSE(editor::CanInsertAt(run, 3.2f, c_Step));
		CHECK_FALSE(editor::CanInsertAt(run, 1.5f, c_Step));
		CHECK_FALSE(editor::CanInsertAt(run, 6.0f, c_Step));
	}

	SECTION("anything that would display as a sample already there is refused")
	{
		// Nearer than the box can show is a duplicate on screen whatever the float says.
		CHECK_FALSE(editor::CanInsertAt(run, 3.2004f, c_Step));
		CHECK_FALSE(editor::CanInsertAt(run, 3.1996f, c_Step));
	}

	SECTION("the next value the box can offer is accepted")
	{
		// The case a distance comparison gets wrong: 3.21f minus 3.2f is 0.0099999905, a hair under
		// the step, so subtracting would refuse the very value a nudge of the box produces.
		CHECK(editor::CanInsertAt(run, 3.21f, c_Step));
		CHECK(editor::CanInsertAt(run, 3.19f, c_Step));
	}

	SECTION("a precision of nothing admits nothing")
	{
		CHECK_FALSE(editor::CanInsertAt(run, 4.0f, 0.0f));
	}

	SECTION("a parameter far past what a step count can address is still answered")
	{
		// Both doors admit any finite parameter, so a `.bblend` written by hand can carry this and
		// it reaches here the moment the panel opens one. Rounding it over the box's step is
		// undefined rather than merely large, so the ratio is held inside what a long long holds.
		CHECK(editor::CanInsertAt(run, 1e20f, c_Step));
		CHECK(editor::CanInsertAt(run, -1e20f, c_Step));

		// Two that saturate read as one value, which is the truth about them: at that magnitude a
		// float's own step is wider than any precision a box could show.
		const std::vector<assetlib::BlendSpaceSample> huge = { { "a", 0.0f }, { "b", 1e20f } };
		CHECK_FALSE(editor::CanInsertAt(huge, 1e20f, c_Step));
	}

	SECTION("a parameter that is not a number is refused")
	{
		CHECK_FALSE(editor::CanInsertAt(run, std::numeric_limits<float>::quiet_NaN(), c_Step));
		CHECK_FALSE(editor::CanInsertAt(run, std::numeric_limits<float>::infinity(), c_Step));
	}
}

TEST_CASE("A threshold is held between its neighbours", "[animation][blend]")
{
	const std::vector<assetlib::BlendSpaceSample> run = Run();

	SECTION("inside the interval it moves freely")
	{
		CHECK(editor::ClampedParameter(run, 1, 4.0f, c_Step) == Catch::Approx(4.0f));
	}

	SECTION("dragged onto a neighbour it stops a gap short")
	{
		CHECK(editor::ClampedParameter(run, 1, 99.0f, c_Step) == Catch::Approx(6.0f - c_Step));
		CHECK(editor::ClampedParameter(run, 1, -99.0f, c_Step) == Catch::Approx(1.5f + c_Step));
	}

	SECTION("the ends are open, so the run's extent is authored by dragging them")
	{
		CHECK(editor::ClampedParameter(run, 0, -50.0f, c_Step) == Catch::Approx(-50.0f));
		CHECK(editor::ClampedParameter(run, 2, 50.0f, c_Step) == Catch::Approx(50.0f));

		// ...but they still cannot cross inward past their one neighbour.
		CHECK(editor::ClampedParameter(run, 0, 99.0f, c_Step) == Catch::Approx(3.2f - c_Step));
		CHECK(editor::ClampedParameter(run, 2, -99.0f, c_Step) == Catch::Approx(3.2f + c_Step));
	}

	SECTION("every clamped result still strictly increases")
	{
		// The property the whole function exists for, swept rather than sampled: wherever the
		// middle sample is dragged, the run it lands in is one AddRig would take.
		for (int step = -200; step <= 200; ++step)
		{
			const float wanted  = static_cast<float>(step) * 0.1f;
			const float clamped = editor::ClampedParameter(run, 1, wanted, c_Step);

			INFO("dragged to " << wanted);
			CHECK(clamped > run[0].parameter);
			CHECK(clamped < run[2].parameter);
		}
	}

	SECTION("a parameter that is not a number leaves the sample where it is")
	{
		CHECK(
			editor::ClampedParameter(run, 1, std::numeric_limits<float>::quiet_NaN(), c_Step) ==
			Catch::Approx(3.2f));
	}

	SECTION("an index past the run is answered unchanged")
	{
		CHECK(editor::ClampedParameter(run, 9, 4.0f, c_Step) == Catch::Approx(4.0f));
	}

	SECTION("neighbours with no room between them hold the sample still")
	{
		// Authored by hand rather than reachable through this surface, but a run read off disk can
		// be anything, and moving a sample to a value that breaks the order is worse than refusing.
		const std::vector<assetlib::BlendSpaceSample> tight = { { "a", 1.0f },
			                                                    { "b", 1.001f },
			                                                    { "c", 1.002f } };
		CHECK(editor::ClampedParameter(tight, 1, 5.0f, c_Step) == Catch::Approx(1.001f));
	}
}

TEST_CASE("A space keeps at least two samples", "[animation][blend]")
{
	CHECK(editor::CanRemoveSample(Run()));

	const std::vector<assetlib::BlendSpaceSample> pair = { { "walk", 0.0f }, { "run", 1.0f } };
	CHECK_FALSE(editor::CanRemoveSample(pair));
}

TEST_CASE("A space's name is one the set can hold", "[animation][blend]")
{
	std::vector<assetlib::BlendSpace> spaces(1);
	spaces[0].name = "locomotion";

	CHECK(editor::CanNameSpace(spaces, "strafe"));
	CHECK_FALSE(editor::CanNameSpace(spaces, "locomotion"));
	CHECK_FALSE(editor::CanNameSpace(spaces, ""));
}

TEST_CASE("The cursor maps a Scrubber's ticks onto the run's parameters", "[animation][blend]")
{
	constexpr int   c_Ticks = 1000;
	constexpr float c_Min   = 1.5f;
	constexpr float c_Max   = 6.0f;

	SECTION("the ends are the run's own extent")
	{
		CHECK(editor::ParameterForTick(c_Min, c_Max, c_Ticks, 0) == Catch::Approx(c_Min));
		CHECK(editor::ParameterForTick(c_Min, c_Max, c_Ticks, c_Ticks) == Catch::Approx(c_Max));
	}

	SECTION("the middle tick is the middle parameter")
	{
		CHECK(
			editor::ParameterForTick(c_Min, c_Max, c_Ticks, c_Ticks / 2) ==
			Catch::Approx(3.75f).margin(1e-4));
	}

	SECTION("a tick outside the bar is clamped onto it")
	{
		CHECK(editor::ParameterForTick(c_Min, c_Max, c_Ticks, -50) == Catch::Approx(c_Min));
		CHECK(
			editor::ParameterForTick(c_Min, c_Max, c_Ticks, c_Ticks + 50) == Catch::Approx(c_Max));
	}

	SECTION("the two directions agree, so a typed value and the thumb do not drift apart")
	{
		for (int tick = 0; tick <= c_Ticks; tick += 37)
		{
			const float parameter = editor::ParameterForTick(c_Min, c_Max, c_Ticks, tick);
			INFO("tick " << tick << " -> " << parameter);
			CHECK(editor::TickForParameter(c_Min, c_Max, c_Ticks, parameter) == tick);
		}
	}

	SECTION("a parameter off the ends lands on them")
	{
		CHECK(editor::TickForParameter(c_Min, c_Max, c_Ticks, -10.0f) == 0);
		CHECK(editor::TickForParameter(c_Min, c_Max, c_Ticks, 99.0f) == c_Ticks);
	}

	SECTION("a collapsed run has one parameter to offer and no cursor position but the first")
	{
		CHECK(editor::ParameterForTick(4.0f, 4.0f, c_Ticks, 500) == Catch::Approx(4.0f));
		CHECK(editor::TickForParameter(4.0f, 4.0f, c_Ticks, 4.0f) == 0);
		CHECK(editor::ParameterForTick(c_Min, c_Max, 0, 5) == Catch::Approx(c_Min));
	}

	SECTION("a parameter far outside the run lands on an end rather than anywhere")
	{
		// Scaled after the clamp, not before: the product would otherwise be past what a long
		// holds, and rounding it is undefined rather than out of range.
		CHECK(editor::TickForParameter(c_Min, c_Max, c_Ticks, 1e20f) == c_Ticks);
		CHECK(editor::TickForParameter(c_Min, c_Max, c_Ticks, -1e20f) == 0);
	}

	SECTION("a parameter that is not a number answers the first tick")
	{
		CHECK(
			editor::TickForParameter(
				c_Min,
				c_Max,
				c_Ticks,
				std::numeric_limits<float>::quiet_NaN()) == 0);
	}
}
