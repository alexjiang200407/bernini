#pragma once
#include <gamelib/BlendSpaceInfo.h>
#include <gamelib/ClipInfo.h>

#include <bgl/InstanceDesc.h>

namespace game
{
	/**
	 * The writes a playback record takes between frames: start a crossfade, move a blend space's
	 * parameter. Pure functions from a record and a clock to the next record, so a caller keeps its
	 * own copy, hands the result to `ISceneView::SetSkinnedPlayback`, and nothing here holds state.
	 *
	 * **Every write only changes the future.** A record is read at two clocks each frame -- `time`
	 * and `prevTime` -- and the pair is what the motion vector is derived from, so a rewrite that
	 * changed what the record says about `prevTime` would reproject through a pose nothing drew.
	 * These keep that: a slot that is still playing keeps its phase, its rate and its reference
	 * time untouched, so it evaluates to the same frame at every clock, and a slot being faded out
	 * is ramped down rather than dropped.
	 *
	 * The one thing that is not exact is a *weight* interrupted mid-ramp. A slot holds a single
	 * line -- two values and a window -- so it cannot say one thing before `now` and another after,
	 * and a fade begun while an earlier fade was still running restates the weight from `now`. It
	 * is wrong at one evaluation only, `prevTime` on the frame the write happens, and only when a
	 * fade interrupts a fade; the pose itself is exact throughout.
	 *
	 * *One evaluation* is the claim, not *a small error*: how far it is out is how far the old ramp
	 * moved between `prevTime` and `now`, which for a fade shorter than a frame is the whole weight.
	 * Seeding from `prevTime` instead would trade this for a wrong weight in the frame actually
	 * drawn, which is worse, and no single line per slot avoids both. A second breakpoint in the
	 * record is what it would take, which is not worth it for one frame of one motion vector.
	 */

	/** The weight `slot` carries at `time`, as the pose pass reads it. */
	[[nodiscard]] float
	SlotWeightAt(const bgl::PlaybackSlot& slot, float time) noexcept;

	/** The parameter `slot` carries at `time`, as the pose pass reads it. */
	[[nodiscard]] float
	SlotParameterAt(const bgl::PlaybackSlot& slot, float time) noexcept;

	/**
	 * `desc` with a fade onto `node` begun at `now` and finished `duration` later: every slot that
	 * carries weight ramps to zero over that window, and one slot takes `node` up from zero.
	 *
	 * The incoming slot starts at `phase` with `tRef = now`, so it begins where the caller says
	 * rather than where the clock happens to be.
	 *
	 * A record already fading onto `node` is *not* restarted -- the same request twice in
	 * consecutive frames would otherwise never arrive. The incoming slot is the one already playing
	 * it, and its ramp is left alone.
	 *
	 * When every slot is taken, the lightest at `now` is evicted outright. That is the one place a
	 * pop is accepted, and it takes three fades interrupting each other inside one window to reach.
	 *
	 * @throws std::runtime_error if `duration` is negative or either it or `now` is not finite.
	 */
	[[nodiscard]] bgl::SkinnedPlaybackDesc
	CrossfadeTo(
		const bgl::SkinnedPlaybackDesc& desc,
		uint32_t                        node,
		float                           now,
		float                           duration,
		float                           phase = 0.0f,
		float                           rate  = 1.0f);

	/**
	 * `desc` with the slot playing `node` moved to `parameter` over `duration` from `now`.
	 *
	 * The slot's phase is rebased to `now` first, and that is the whole subtlety: a space's phase
	 * advances at the reciprocal of the weighted cycle length, so a parameter that moves changes
	 * the rate it advances at. Left alone, the pose pass would integrate the *new* parameter path
	 * from the old reference time and land somewhere the old record never was -- a jump, on the
	 * frame the write happens. Rebasing carries what the old path already covered into `phase` and
	 * starts the new one at `now`.
	 *
	 * `space` and `clips` are the acquire's, and are what the cycle lengths come from.
	 *
	 * A `node` no slot is playing is a no-op: there is nothing to steer, and a caller that
	 * retargets before it crossfades should not silently start playing something.
	 *
	 * @throws std::runtime_error if `duration` is negative, `now` or `duration` is not finite, or a
	 *         member names a clip outside `clips`.
	 */
	[[nodiscard]] bgl::SkinnedPlaybackDesc
	RetargetParameter(
		const bgl::SkinnedPlaybackDesc& desc,
		uint32_t                        node,
		const BlendSpaceInfo&           space,
		std::span<const ClipInfo>       clips,
		float                           parameter,
		float                           now,
		float                           duration);
}
