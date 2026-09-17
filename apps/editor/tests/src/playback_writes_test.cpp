#include "Windows/AnimationEditor/playback_writes.h"

#include "Windows/AnimationEditor/transition_spans.h"

#include <bgl/InstanceDesc.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <gamelib/anim_blend.h>
#include <optional>

// The two rules behind the Animation panel's clip switch: which pose source takes a record rewrite
// at all, and -- for the one that does not -- which single clip a respawn lands on so the switch
// shows the pose the record already had.

namespace
{
	using editor::DominantNode;
	using editor::RewritesPlayback;

	constexpr float c_Now = 10.0f;
}

TEST_CASE("Only the per-instance source takes a playback rewrite", "[animation]")
{
	// The crowd source reads the rig's shared table: one clip, no slots, and no instance may write
	// it. SetSkinnedPlayback throws there, so the panel must respawn instead.
	CHECK(RewritesPlayback(bgl::PoseSource::kPerInstance));
	CHECK_FALSE(RewritesPlayback(bgl::PoseSource::kBoneAnimTable));
}

TEST_CASE("A record playing one clip is dominated by it", "[animation]")
{
	const auto desc = bgl::SkinnedPlaybackDesc::FromClip(3);

	CHECK(DominantNode(desc, c_Now) == 3);

	// FromClip's weights are flat, so the answer does not depend on the clock.
	CHECK(DominantNode(desc, 0.0f) == 3);
	CHECK(DominantNode(desc, 1000.0f) == 3);
}

TEST_CASE("A record mid-crossfade is dominated by whichever slot is ahead", "[animation]")
{
	const auto before = bgl::SkinnedPlaybackDesc::FromClip(1);
	const auto fading = game::CrossfadeTo(before, 2, c_Now, 1.0f);

	// The outgoing slot still carries the pose as the fade opens.
	CHECK(DominantNode(fading, c_Now) == 1);

	// Past the halfway point the incoming one has more of it.
	CHECK(DominantNode(fading, c_Now + 0.6f) == 2);

	// And once the ramps have run, only the incoming slot is left.
	CHECK(DominantNode(fading, c_Now + 1.0f) == 2);
	CHECK(DominantNode(fading, c_Now + 50.0f) == 2);
}

TEST_CASE("A fade of no duration evicts the clip it replaces, and forgets the past", "[animation]")
{
	// Why the panel's clip switch is a respawn rather than a rewrite. Every ramp CrossfadeTo writes
	// has already completed at `now` when the duration is zero, so every slot reads weight zero
	// there -- including the outgoing one it just ramped down. The eviction search then finds the
	// outgoing slot no heavier than a free one and overwrites it, so the record no longer says at
	// any earlier clock what the frame before it drew.
	const auto before  = bgl::SkinnedPlaybackDesc::FromClip(1);
	const auto instant = game::CrossfadeTo(before, 2, c_Now, 0.0f);

	CHECK(DominantNode(instant, c_Now) == 2);
	CHECK(DominantNode(instant, c_Now - 0.001f) == 2);

	// A fade with a window keeps it, which is the case the strip drives and this one is not.
	const auto fading = game::CrossfadeTo(before, 2, c_Now, 0.25f);
	CHECK(DominantNode(fading, c_Now - 0.001f) == 1);
}

TEST_CASE("A record whose ramps are all ahead of the clock answers slot 0", "[animation]")
{
	// InstanceDesc.h: at a clock where no slot carries weight, slot 0 plays alone at full weight.
	auto desc              = bgl::SkinnedPlaybackDesc();
	desc.slot[0].nodeIndex = 7;

	desc.slot[1].nodeIndex = 9;
	desc.slot[1].weight0   = 0.0f;
	desc.slot[1].weight1   = 1.0f;
	desc.slot[1].rampStart = c_Now + 5.0f;
	desc.slot[1].rampEnd   = c_Now + 6.0f;

	CHECK(DominantNode(desc, c_Now) == 7);

	// Once slot 1's ramp has run it is the one being shown.
	CHECK(DominantNode(desc, c_Now + 6.0f) == 9);
}

TEST_CASE("A tie keeps the lower slot", "[animation]")
{
	auto desc              = bgl::SkinnedPlaybackDesc();
	desc.slot[0].nodeIndex = 4;
	desc.slot[0].weight0   = 0.5f;
	desc.slot[0].weight1   = 0.5f;

	desc.slot[1].nodeIndex = 5;
	desc.slot[1].weight0   = 0.5f;
	desc.slot[1].weight1   = 0.5f;

	CHECK(DominantNode(desc, c_Now) == 4);
}

TEST_CASE("A cut is one sample interval, and never zero", "[animation]")
{
	CHECK(editor::CutSeconds(30.0f) == Catch::Approx(1.0f / 30.0f));
	CHECK(editor::CutSeconds(60.0f) == Catch::Approx(1.0f / 60.0f));

	// Zero would not cut at all. CrossfadeTo's ramps have already completed at the clock they are
	// written at, so every slot reads zero weight there, the eviction search takes the outgoing
	// slot, and the record shows the destination at every earlier clock -- the whole window rather
	// than the half after the cut. The case below is that behaviour, stated from the other side.
	const auto before  = bgl::SkinnedPlaybackDesc::FromClip(1);
	const auto instant = game::CrossfadeTo(before, 2, c_Now, 0.0f);
	CHECK(DominantNode(instant, c_Now - 1.0f) == 2);

	const auto cut = game::CrossfadeTo(before, 2, c_Now, editor::CutSeconds(30.0f));
	CHECK(DominantNode(cut, c_Now - 1.0f) == 1);  // the outgoing clip still plays up to the cut
	CHECK(DominantNode(cut, c_Now + 1.0f) == 2);  // and the incoming one after it

	// A rate a clip could not really have still yields something drawable.
	CHECK(editor::CutSeconds(0.0f) > 0.0f);
	CHECK(editor::CutSeconds(-5.0f) > 0.0f);
}

TEST_CASE("A previewed transition plays its outgoing end from the window's start", "[animation]")
{
	// A 0.7 s one-shot faded into a loop, as the Blend tab stamps it: the clock is absolute and sits
	// near c_Now, so an outgoing end anchored at zero would already have clamped to its last frame
	// and shown a still pose through the whole run-up.
	constexpr uint32_t c_Roll       = 0;
	constexpr uint32_t c_Run        = 1;
	constexpr float    c_SampleRate = 30.0f;

	const auto layout = editor::WindowFor(c_Now, 0.25f, 0.6f, 0.9f);
	const auto desc   = editor::TransitionPlayback(c_Roll, c_Run, 0.0f, 0.0f, layout);

	const bgl::PlaybackSlot& from = desc.slot[0];
	REQUIRE(from.nodeIndex == c_Roll);
	CHECK(from.tRef == Catch::Approx(layout.windowStart));

	// InstanceDesc.h's frame: phase advanced by (t - tRef) * rate * sampleRate.
	const auto frameAt = [&](const float t) {
		return from.phase + (t - from.tRef) * from.rate * c_SampleRate;
	};
	CHECK(frameAt(layout.windowStart) == Catch::Approx(0.0f));
	CHECK(frameAt(layout.start) == Catch::Approx(0.6f * c_SampleRate));

	// The outgoing end is still the pose when the window opens, and the incoming one takes over.
	CHECK(DominantNode(desc, layout.windowStart) == c_Roll);
	CHECK(DominantNode(desc, layout.windowEnd) == c_Run);
}

TEST_CASE("A previewed transition carries each end's blend-space parameter", "[animation]")
{
	const auto layout = editor::WindowFor(c_Now, 0.5f, 0.6f, 0.9f);
	const auto desc   = editor::TransitionPlayback(4, 5, 0.25f, 0.75f, layout);

	CHECK(desc.slot[0].param0 == Catch::Approx(0.25f));
	CHECK(desc.slot[0].param1 == Catch::Approx(0.25f));

	const bool incomingAtThreeQuarters = [&] {
		for (const bgl::PlaybackSlot& slot : desc.slot)
		{
			if (slot.nodeIndex == 5 && slot.param1 == Catch::Approx(0.75f))
				return true;
		}
		return false;
	}();
	CHECK(incomingAtThreeQuarters);
}

TEST_CASE("The Blend tab plays From alone until a fade is named", "[animation]")
{
	using editor::SoloFromClip;

	// To unset: changing From must change what plays.
	CHECK(SoloFromClip(2, -1) == std::optional<int>(2));
	CHECK(SoloFromClip(5, -1) == std::optional<int>(5));

	// To naming From is no fade either.
	CHECK(SoloFromClip(3, 3) == std::optional<int>(3));

	// A fade, or no From at all, plays nothing on its own.
	CHECK_FALSE(SoloFromClip(2, 4).has_value());
	CHECK_FALSE(SoloFromClip(-1, -1).has_value());
	CHECK_FALSE(SoloFromClip(-1, 4).has_value());
}
