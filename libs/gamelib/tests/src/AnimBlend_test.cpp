#include <gamelib/anim_blend.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <cstddef>
#include <gamelib/BlendSpaceInfo.h>

// The writes a playback record takes between frames. No device and no scene: these are pure
// functions from a record and a clock to the next record, which is the whole reason they are
// separable from the pass that reads them.
//
// What every case here is really checking is one property -- a write only changes the future -- so
// the pose the frame before this one drew is still what the record says at `prevTime`.

using game::CrossfadeTo;
using game::RetargetParameter;
using game::SlotParameterAt;
using game::SlotWeightAt;

namespace
{
	/** A record playing node `node` alone, since a spawn is what a caller starts from. */
	bgl::SkinnedPlaybackDesc
	Playing(uint32_t node)
	{
		return bgl::SkinnedPlaybackDesc::FromClip(node);
	}

	/** The slot playing `node`, or none. */
	const bgl::PlaybackSlot*
	SlotFor(const bgl::SkinnedPlaybackDesc& desc, uint32_t node)
	{
		for (const bgl::PlaybackSlot& slot : desc.slot)
			if (slot.nodeIndex == node && (slot.weight0 > 0.0f || slot.weight1 > 0.0f))
				return &slot;

		return nullptr;
	}

	/** Every live weight at `time`, normalized as the pose pass normalizes them. */
	float
	TotalWeightAt(const bgl::SkinnedPlaybackDesc& desc, float time)
	{
		float total = 0.0f;
		for (const bgl::PlaybackSlot& slot : desc.slot) total += SlotWeightAt(slot, time);
		return total;
	}

	/** Two looping clips of different lengths, which is what a space's shared phase needs. */
	std::vector<game::ClipInfo>
	MakeClips()
	{
		auto walk       = game::ClipInfo();
		walk.name       = "walk";
		walk.frameCount = 3;  // two intervals
		walk.sampleRate = 30.0f;
		walk.loop       = true;

		auto run       = game::ClipInfo();
		run.name       = "run";
		run.frameCount = 5;  // four intervals
		run.sampleRate = 30.0f;
		run.loop       = true;

		auto sprint       = game::ClipInfo();
		sprint.name       = "sprint";
		sprint.frameCount = 9;  // eight intervals
		sprint.sampleRate = 30.0f;
		sprint.loop       = true;

		return { walk, run, sprint };
	}

	game::BlendSpaceInfo
	MakeSpace()
	{
		auto space = game::BlendSpaceInfo();
		space.name = "locomotion";
		space.samples.push_back({ 0, 0.0f });
		space.samples.push_back({ 1, 1.0f });
		return space;
	}

	/**
	 * Four samples, so a ramp across them crosses two interior ones. Two is the point: with a
	 * single crossing the walk visits the same edge whichever direction it takes, and the order it
	 * accumulates segments in cannot be observed.
	 */
	game::BlendSpaceInfo
	MakeWideSpace()
	{
		auto space = game::BlendSpaceInfo();
		space.name = "wide";
		space.samples.push_back({ 0, 0.0f });
		space.samples.push_back({ 1, 0.33f });
		space.samples.push_back({ 2, 0.66f });
		space.samples.push_back({ 0, 1.0f });
		return space;
	}

	/** A sample's cycle in seconds: the intervals it wraps over, at its authored rate. */
	double
	CycleSeconds(const game::ClipInfo& clip)
	{
		return double(clip.frameCount - 1) / double(clip.sampleRate);
	}

	/**
	 * The phase a slot on `space` reaches at `now`, by stepping the integral the closed form is
	 * supposed to evaluate. In double: at this step count a float sum loses more precision than the
	 * thing it is checking.
	 */
	float
	SteppedPhase(
		const bgl::PlaybackSlot&           slot,
		const game::BlendSpaceInfo&        space,
		const std::vector<game::ClipInfo>& clips,
		float                              now)
	{
		const auto secondsAt = [&](double p) {
			const auto& samples = space.samples;
			if (p <= samples.front().parameter)
				return CycleSeconds(clips[samples.front().clipIndex]);
			if (p >= samples.back().parameter)
				return CycleSeconds(clips[samples.back().clipIndex]);

			for (size_t i = 1; i < samples.size(); ++i)
			{
				if (p <= samples[i].parameter)
				{
					const double a = samples[i - 1].parameter;
					const double b = samples[i].parameter;
					const double w = (p - a) / (b - a);
					return std::lerp(
						CycleSeconds(clips[samples[i - 1].clipIndex]),
						CycleSeconds(clips[samples[i].clipIndex]),
						w);
				}
			}
			return CycleSeconds(clips[samples.back().clipIndex]);
		};

		const auto parameterAt = [&](double t) {
			if (t < slot.paramStart)
				return double(slot.param0);
			if (t >= slot.paramEnd)
				return double(slot.param1);
			const double w = (t - slot.paramStart) / (double(slot.paramEnd) - slot.paramStart);
			return std::lerp(double(slot.param0), double(slot.param1), w);
		};

		constexpr uint32_t c_Steps  = 200000;
		const double       step     = (double(now) - slot.tRef) / double(c_Steps);
		double             integral = 0.0;
		for (uint32_t i = 0; i < c_Steps; ++i)
		{
			const double mid = double(slot.tRef) + (double(i) + 0.5) * step;
			integral += step / secondsAt(parameterAt(mid));
		}

		const double phase = double(slot.phase) + double(slot.rate) * integral;
		return static_cast<float>(phase - std::floor(phase));
	}
}

TEST_CASE("a crossfade ramps the old slot down instead of dropping it", "[gamelib][animblend]")
{
	constexpr float c_Now      = 10.0f;
	constexpr float c_Duration = 0.25f;

	const auto before = Playing(0);
	const auto after  = CrossfadeTo(before, 1, c_Now, c_Duration);

	const bgl::PlaybackSlot* outgoing = SlotFor(after, 0);
	const bgl::PlaybackSlot* incoming = SlotFor(after, 1);
	REQUIRE(outgoing != nullptr);
	REQUIRE(incoming != nullptr);

	SECTION("the outgoing slot is still there, on its way down")
	{
		CHECK(SlotWeightAt(*outgoing, c_Now) == Catch::Approx(1.0f));
		CHECK(SlotWeightAt(*outgoing, c_Now + c_Duration) == Catch::Approx(0.0f));
	}

	SECTION("the incoming slot arrives over the same window")
	{
		CHECK(SlotWeightAt(*incoming, c_Now) == Catch::Approx(0.0f));
		CHECK(SlotWeightAt(*incoming, c_Now + c_Duration) == Catch::Approx(1.0f));
	}

	SECTION("what the outgoing slot plays is untouched, at every clock")
	{
		// The pose at prevTime is the property this protects: phase, rate and reference time are
		// what say which frame it shows, and a write must not move any of them.
		const bgl::PlaybackSlot& was = before.slot[0];
		CHECK(outgoing->phase == was.phase);
		CHECK(outgoing->rate == was.rate);
		CHECK(outgoing->tRef == was.tRef);
	}

	SECTION("nothing changes before the write")
	{
		// Before `now` the record still weighs what it did, which is what makes the frame before
		// this one reproject onto the pose it actually drew.
		CHECK(SlotWeightAt(*outgoing, c_Now - 0.016f) == Catch::Approx(1.0f));
		CHECK(SlotWeightAt(*incoming, c_Now - 0.016f) == Catch::Approx(0.0f));
	}

	SECTION("the pose is a whole one throughout the fade")
	{
		for (const float t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
			CHECK(TotalWeightAt(after, c_Now + t * c_Duration) == Catch::Approx(1.0f));
	}
}

TEST_CASE(
	"an interrupted crossfade ramps the victim down rather than dropping it",
	"[gamelib][animblend]")
{
	const auto first  = CrossfadeTo(Playing(0), 1, 10.0f, 1.0f);
	const auto second = CrossfadeTo(first, 2, 10.5f, 1.0f);

	// Mid-fade, so all three carry weight: the one being left, the one it was going to, and the one
	// it is going to now. This is what four slots were sized for.
	const bgl::PlaybackSlot* leaving   = SlotFor(second, 0);
	const bgl::PlaybackSlot* abandoned = SlotFor(second, 1);
	const bgl::PlaybackSlot* arriving  = SlotFor(second, 2);
	REQUIRE(leaving != nullptr);
	REQUIRE(abandoned != nullptr);
	REQUIRE(arriving != nullptr);

	SECTION("the abandoned target is ramped down, not cut")
	{
		CHECK(SlotWeightAt(*abandoned, 10.5f) == Catch::Approx(0.5f));
		CHECK(SlotWeightAt(*abandoned, 11.5f) == Catch::Approx(0.0f));
	}

	SECTION("and what it plays is still untouched")
	{
		CHECK(abandoned->tRef == 10.0f);
		CHECK(arriving->tRef == 10.5f);
	}

	SECTION("the pose stays whole across the interruption")
	{
		for (const float t : { 10.5f, 11.0f, 11.5f, 12.0f })
			CHECK(TotalWeightAt(second, t) == Catch::Approx(1.0f));
	}
}

TEST_CASE("a fifth node evicts the lightest slot", "[gamelib][animblend]")
{
	// Four slots, and a fade needs one for the arriving node. Three interruptions inside one window
	// is what it takes to fill them, which is the cost ADR-8 accepted.
	auto desc = Playing(0);
	desc      = CrossfadeTo(desc, 1, 10.0f, 1.0f);
	desc      = CrossfadeTo(desc, 2, 10.25f, 1.0f);
	desc      = CrossfadeTo(desc, 3, 10.5f, 1.0f);

	// Which one is lightest is not the one that has been fading longest: each interruption restates
	// every weight from where it had got to, so they do not decay in the order they started.
	uint32_t quietest = desc.slot[0].nodeIndex;
	float    least    = SlotWeightAt(desc.slot[0], 10.75f);
	for (const bgl::PlaybackSlot& slot : desc.slot)
	{
		if (SlotWeightAt(slot, 10.75f) < least)
		{
			least    = SlotWeightAt(slot, 10.75f);
			quietest = slot.nodeIndex;
		}
	}

	const auto evicted = CrossfadeTo(desc, 4, 10.75f, 1.0f);

	CHECK(SlotFor(evicted, 4) != nullptr);
	CHECK(SlotFor(evicted, quietest) == nullptr);

	// Everything louder than it survives, still ramping down.
	for (const bgl::PlaybackSlot& slot : desc.slot)
	{
		if (slot.nodeIndex != quietest)
			CHECK(SlotFor(evicted, slot.nodeIndex) != nullptr);
	}
}

TEST_CASE("a crossfade onto what is already arriving does not restart it", "[gamelib][animblend]")
{
	// The same request in consecutive frames is what a state machine issues while a condition
	// holds; restarting each time would mean the fade never finishes.
	const auto first = CrossfadeTo(Playing(0), 1, 10.0f, 1.0f);
	const auto again = CrossfadeTo(first, 1, 10.1f, 1.0f);

	const bgl::PlaybackSlot* arriving = SlotFor(again, 1);
	REQUIRE(arriving != nullptr);
	CHECK(arriving->rampStart == 10.0f);
	CHECK(arriving->rampEnd == 11.0f);
}

TEST_CASE("a crossfade says what parameter a space arrives at", "[gamelib][animblend]")
{
	// A fade names a node, and a space node plays nothing until something says where on its axis.
	// Without this the slot arrives at zero -- the bottom of the run -- with nothing refused.
	constexpr uint32_t c_SpaceNode = 2;  // two clips, so the space is node 2
	constexpr float    c_Now       = 10.0f;
	constexpr float    c_Duration  = 0.5f;
	constexpr float    c_Parameter = 2.5f;

	SECTION("the arriving slot holds the parameter it was given, at every clock")
	{
		const auto after =
			CrossfadeTo(Playing(0), c_SpaceNode, c_Now, c_Duration, 0.0f, 1.0f, c_Parameter);

		const bgl::PlaybackSlot* arriving = SlotFor(after, c_SpaceNode);
		REQUIRE(arriving != nullptr);

		// One value rather than a ramp, so the fade reads the same before, during and after its
		// window -- a parameter that moves is RetargetParameter's write, not this one's.
		CHECK(SlotParameterAt(*arriving, c_Now - 1.0f) == Catch::Approx(c_Parameter));
		CHECK(SlotParameterAt(*arriving, c_Now) == Catch::Approx(c_Parameter));
		CHECK(SlotParameterAt(*arriving, c_Now + c_Duration) == Catch::Approx(c_Parameter));

		// The ends as fields, because no clock reaches param0 here: a fresh slot's window is
		// degenerate -- paramStart and paramEnd both zero -- so SlotParameterAt returns param1 at
		// every time a record is ever read at. param0 == param1 *is* the "one value" claim, and it
		// is what a later retarget ramps away from.
		CHECK(arriving->param0 == Catch::Approx(c_Parameter));
		CHECK(arriving->param1 == Catch::Approx(c_Parameter));
		CHECK(arriving->paramStart == arriving->paramEnd);
	}

	SECTION("a slot already at that node keeps the parameter it had")
	{
		// Fading back to what is showing reuses its slot rather than uploading a second copy, and
		// the parameter goes with the phase and the rate: where it already is, steering is a
		// retarget's job.
		auto playing              = bgl::SkinnedPlaybackDesc();
		playing.slot[0].nodeIndex = c_SpaceNode;
		playing.slot[0].weight0   = 1.0f;
		playing.slot[0].weight1   = 1.0f;
		playing.slot[0].param0    = 4.0f;
		playing.slot[0].param1    = 4.0f;

		const auto after =
			CrossfadeTo(playing, c_SpaceNode, c_Now, c_Duration, 0.0f, 1.0f, c_Parameter);

		const bgl::PlaybackSlot* arriving = SlotFor(after, c_SpaceNode);
		REQUIRE(arriving != nullptr);
		CHECK(SlotParameterAt(*arriving, c_Now) == Catch::Approx(4.0f));
	}

	SECTION("the default is what every caller before this said")
	{
		const auto after = CrossfadeTo(Playing(0), 1, c_Now, c_Duration);

		const bgl::PlaybackSlot* arriving = SlotFor(after, 1);
		REQUIRE(arriving != nullptr);
		CHECK(arriving->param0 == 0.0f);
		CHECK(arriving->param1 == 0.0f);
		CHECK(arriving->paramStart == 0.0f);
		CHECK(arriving->paramEnd == 0.0f);
	}
}

TEST_CASE("a retarget rebases the phase it had already reached", "[gamelib][animblend]")
{
	const std::vector<game::ClipInfo> clips = MakeClips();
	const game::BlendSpaceInfo        space = MakeSpace();

	constexpr uint32_t c_SpaceNode = 2;  // two clips, so the space is node 2

	auto desc              = bgl::SkinnedPlaybackDesc();
	desc.slot[0].nodeIndex = c_SpaceNode;
	desc.slot[0].rate      = 1.0f;
	desc.slot[0].tRef      = 0.0f;
	desc.slot[0].phase     = 0.0f;
	desc.slot[0].weight0   = 1.0f;
	desc.slot[0].weight1   = 1.0f;

	SECTION("the phase carried in is what the old path had reached")
	{
		constexpr float c_Now = 0.05f;

		// Parameter held at 0, so the cycle is clip 0's: two intervals at 30 Hz, a fifteenth of a
		// second. The phase at `now` is that many cycles in, wrapped.
		const float cycle    = 2.0f / 30.0f;
		const float expected = std::fmod(c_Now / cycle, 1.0f);

		const auto after = RetargetParameter(desc, c_SpaceNode, space, clips, 1.0f, c_Now, 0.5f);

		REQUIRE(SlotFor(after, c_SpaceNode) != nullptr);
		CHECK(SlotFor(after, c_SpaceNode)->phase == Catch::Approx(expected).margin(1e-5));
		CHECK(SlotFor(after, c_SpaceNode)->tRef == c_Now);
	}

	SECTION("the parameter ramps from where it was to where it was asked for")
	{
		const auto after = RetargetParameter(desc, c_SpaceNode, space, clips, 1.0f, 0.05f, 0.5f);

		const bgl::PlaybackSlot* slot = SlotFor(after, c_SpaceNode);
		REQUIRE(slot != nullptr);
		CHECK(SlotParameterAt(*slot, 0.05f) == Catch::Approx(0.0f));
		CHECK(SlotParameterAt(*slot, 0.55f) == Catch::Approx(1.0f));
	}

	SECTION("a rebase mid-ramp integrates the path, not just its ends")
	{
		// The closed form has to match the integral it stands for while the parameter is still
		// moving, which is exactly where the approximation ADR-11 rejected goes wrong.
		auto slot       = desc.slot[0];
		slot.param0     = 0.0f;
		slot.param1     = 1.0f;
		slot.paramStart = 0.0f;
		slot.paramEnd   = 0.4f;

		auto ramping    = bgl::SkinnedPlaybackDesc();
		ramping.slot[0] = slot;

		constexpr float c_Now = 0.15f;  // inside the window, not at either end

		const auto after = RetargetParameter(ramping, c_SpaceNode, space, clips, 0.0f, c_Now, 0.5f);

		REQUIRE(SlotFor(after, c_SpaceNode) != nullptr);
		CHECK(
			SlotFor(after, c_SpaceNode)->phase ==
			Catch::Approx(SteppedPhase(slot, space, clips, c_Now)).margin(1e-4));
	}

	SECTION("a falling ramp is integrated in the order it reaches the samples")
	{
		// The regression this exists for, and the one the shader twin of this integral actually
		// shipped: the segments have to be accumulated in the order the ramp reaches them, not in
		// table order. A rising ramp reaches them in table order and hides the difference, so this
		// one falls -- across four samples, so it crosses two interior ones. One crossing is
		// visited in the same place whichever way the table is walked and proves nothing.
		const game::BlendSpaceInfo wide = MakeWideSpace();

		constexpr uint32_t c_WideNode = 4;  // three clips, then the two spaces

		auto slot       = bgl::PlaybackSlot();
		slot.nodeIndex  = c_WideNode;
		slot.rate       = 1.0f;
		slot.tRef       = 0.0f;
		slot.phase      = 0.0f;
		slot.weight0    = 1.0f;
		slot.weight1    = 1.0f;
		slot.param0     = 0.9f;
		slot.param1     = 0.1f;
		slot.paramStart = 0.0f;
		slot.paramEnd   = 0.4f;

		auto ramping    = bgl::SkinnedPlaybackDesc();
		ramping.slot[0] = slot;

		constexpr float c_Now = 0.3f;  // past both interior samples

		const auto after = RetargetParameter(ramping, c_WideNode, wide, clips, 0.5f, c_Now, 0.5f);

		REQUIRE(SlotFor(after, c_WideNode) != nullptr);
		CHECK(
			SlotFor(after, c_WideNode)->phase ==
			Catch::Approx(SteppedPhase(slot, wide, clips, c_Now)).margin(1e-4));
	}

	SECTION("a node no slot is playing is a no-op")
	{
		const auto after = RetargetParameter(desc, 99, space, clips, 1.0f, 0.05f, 0.5f);
		CHECK(SlotFor(after, 99) == nullptr);
		CHECK(SlotFor(after, c_SpaceNode)->phase == desc.slot[0].phase);
	}
}

TEST_CASE("a write that runs backwards is refused", "[gamelib][animblend]")
{
	CHECK_THROWS_WITH(
		CrossfadeTo(Playing(0), 1, 10.0f, -1.0f),
		Catch::Matchers::ContainsSubstring("only changes the future"));

	CHECK_THROWS(RetargetParameter(Playing(0), 0, MakeSpace(), MakeClips(), 1.0f, 10.0f, -1.0f));
}

TEST_CASE("a space says which two samples a parameter is between", "[gamelib][animblend]")
{
	const std::vector<game::ClipInfo> clips = MakeClips();
	const game::BlendSpaceInfo        space = MakeWideSpace();

	// Samples at 0, 0.33, 0.66 and 1, playing clips 0, 1, 2 and 0.
	SECTION("between two samples it is the pair and the fraction")
	{
		const game::BlendSpaceStraddle at = space.StraddleAt(0.495f);
		CHECK(at.lower == 1);
		CHECK(at.upper == 2);
		CHECK(at.weight == Catch::Approx(0.5f));
	}

	SECTION("exactly on a sample it carries that sample alone")
	{
		// The lower end of the span above it, weight zero -- not the span below it at weight one.
		// Either reads as the sample playing alone, and this is the one the walk reaches first.
		const game::BlendSpaceStraddle at = space.StraddleAt(0.33f);
		CHECK(at.lower == 1);
		CHECK(at.upper == 2);
		CHECK(at.weight == 0.0f);
	}

	SECTION("past either end the end sample plays alone")
	{
		const game::BlendSpaceStraddle below = space.StraddleAt(-5.0f);
		CHECK(below.lower == 0);
		CHECK(below.weight == 0.0f);

		// Both indices name the last sample, so a caller reading either one reads that sample.
		const game::BlendSpaceStraddle above = space.StraddleAt(5.0f);
		CHECK(above.lower == 3);
		CHECK(above.upper == 3);
		CHECK(above.weight == 0.0f);
	}

	SECTION("it names an adjacent pair in range, at a weight in range, everywhere")
	{
		// Swept past both ends: the readout is driven by a cursor a person drags, so every
		// parameter it can reach has to name samples a caller may index with.
		for (int step = -2; step <= 22; ++step)
		{
			const float                    parameter = float(step) / 20.0f;
			const game::BlendSpaceStraddle at        = space.StraddleAt(parameter);

			INFO("parameter " << parameter);
			CHECK(at.upper < space.samples.size());
			CHECK((at.upper == at.lower || at.upper == at.lower + 1));
			CHECK(at.weight >= 0.0f);
			CHECK(at.weight <= 1.0f);
		}
	}

	SECTION("it is the walk SecondsAt already made, so the two agree")
	{
		// SecondsAt is written in terms of this, which is the point of extracting it: one straddle
		// rule rather than two that drift.
		for (int step = -2; step <= 22; ++step)
		{
			const float                    parameter = float(step) / 20.0f;
			const game::BlendSpaceStraddle at        = space.StraddleAt(parameter);

			const float expected = std::lerp(
				float(CycleSeconds(clips[space.samples[at.lower].clipIndex])),
				float(CycleSeconds(clips[space.samples[at.upper].clipIndex])),
				at.weight);

			INFO("parameter " << parameter);
			CHECK(space.SecondsAt(clips, parameter) == Catch::Approx(expected));
		}
	}

	SECTION("a two-sample space is the same rule with nothing interior")
	{
		const game::BlendSpaceInfo pair = MakeSpace();

		const game::BlendSpaceStraddle at = pair.StraddleAt(0.25f);
		CHECK(at.lower == 0);
		CHECK(at.upper == 1);
		CHECK(at.weight == Catch::Approx(0.25f));
	}
}
