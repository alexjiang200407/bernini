#include "Windows/AnimationEditor/blend_edits.h"

#include <assetlib/blend.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <gamelib/BlendSpaceInfo.h>
#include <limits>
#include <string>
#include <vector>

#include "Windows/AnimationEditor/PlaybackTransport.h"

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

namespace
{
	// A set of one space over `Run()`, in authored form -- clips by name, which is what a `.bblend`
	// stores and what every rule here takes.
	std::vector<assetlib::BlendSpace>
	Authored()
	{
		auto space    = assetlib::BlendSpace();
		space.name    = "locomotion";
		space.samples = Run();
		return { space };
	}

	// The same space as the acquire resolved it: clip *indices*, in the same order. That positional
	// correspondence is what ApplyParameters relies on.
	std::vector<game::BlendSpaceInfo>
	Resolved()
	{
		auto info    = game::BlendSpaceInfo();
		info.name    = "locomotion";
		info.samples = { { 7, 1.5f }, { 4, 3.2f }, { 9, 6.0f } };
		return { info };
	}
}

TEST_CASE("A threshold that moved goes live; a changed shape does not", "[animation][blend]")
{
	const std::vector<assetlib::BlendSpace> before = Authored();

	SECTION("moving every threshold is still the same set")
	{
		std::vector<assetlib::BlendSpace> after = Authored();
		for (assetlib::BlendSpaceSample& sample : after[0].samples) sample.parameter *= 10.0f;

		CHECK(editor::IsParameterMove(before, after));
	}

	SECTION("adding or removing a sample is a node table that has to be built again")
	{
		std::vector<assetlib::BlendSpace> added = Authored();
		added[0].samples.push_back({ "sprint", 9.0f });
		CHECK_FALSE(editor::IsParameterMove(before, added));

		std::vector<assetlib::BlendSpace> removed = Authored();
		removed[0].samples.pop_back();
		CHECK_FALSE(editor::IsParameterMove(before, removed));
	}

	SECTION("adding or removing a space is too")
	{
		std::vector<assetlib::BlendSpace> added = Authored();
		added.push_back(added[0]);
		added[1].name = "strafe";
		CHECK_FALSE(editor::IsParameterMove(before, added));

		CHECK_FALSE(editor::IsParameterMove(before, {}));
	}

	SECTION("a sample pointed at another clip is not a parameter move")
	{
		// The case that needs the *authored* sets to see at all: the live form holds indices, so a
		// clip swap would read as no change there and reach a rig that refuses it.
		std::vector<assetlib::BlendSpace> swapped = Authored();
		swapped[0].samples[1].clip                = "canter";

		CHECK_FALSE(editor::IsParameterMove(before, swapped));
	}

	SECTION("a renamed space is not one either")
	{
		std::vector<assetlib::BlendSpace> renamed = Authored();
		renamed[0].name                           = "locomotion 2";

		CHECK_FALSE(editor::IsParameterMove(before, renamed));
	}
}

TEST_CASE(
	"A moved threshold reaches the resolved space without a second lookup",
	"[animation][blend]")
{
	std::vector<game::BlendSpaceInfo> live = Resolved();

	SECTION("every parameter is written and no clip index moves")
	{
		std::vector<assetlib::BlendSpace> authored = Authored();
		authored[0].samples[0].parameter           = -2.0f;
		authored[0].samples[1].parameter           = 0.5f;
		authored[0].samples[2].parameter           = 11.0f;

		REQUIRE(editor::ApplyParameters(authored, live));

		CHECK(live[0].samples[0].parameter == Catch::Approx(-2.0f));
		CHECK(live[0].samples[1].parameter == Catch::Approx(0.5f));
		CHECK(live[0].samples[2].parameter == Catch::Approx(11.0f));

		// The indices are what the acquire resolved; nothing here re-resolves a name.
		CHECK(live[0].samples[0].clipIndex == 7);
		CHECK(live[0].samples[1].clipIndex == 4);
		CHECK(live[0].samples[2].clipIndex == 9);
	}

	SECTION("shapes that do not correspond write nothing at all")
	{
		// Checked in full first, so a run that breaks halfway leaves no half-written space -- the
		// same bargain SetRigBlendParameters strikes at the door.
		std::vector<assetlib::BlendSpace> shorter = Authored();
		shorter[0].samples.pop_back();
		shorter[0].samples[0].parameter = 99.0f;

		CHECK_FALSE(editor::ApplyParameters(shorter, live));
		CHECK(live[0].samples[0].parameter == Catch::Approx(1.5f));

		CHECK_FALSE(editor::ApplyParameters({}, live));
		CHECK(live[0].samples[0].parameter == Catch::Approx(1.5f));
	}
}

TEST_CASE("A clip that does not loop cannot be a sample", "[animation][blend]")
{
	auto clip = editor::ClipInfo();
	clip.name = "walk";

	SECTION("a one-shot is refused, and says why")
	{
		clip.loop = false;
		CHECK_FALSE(editor::ClipRefusalReason(clip).empty());
	}

	SECTION("a looping clip is not refused")
	{
		clip.loop = true;
		CHECK(editor::ClipRefusalReason(clip).empty());
	}
}
