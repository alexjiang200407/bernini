#pragma once

#include <assetlib/blend.h>
#include <cstddef>
#include <gamelib/BlendSpaceInfo.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Windows/AnimationEditor/PlaybackTransport.h"

namespace editor
{
	/**
	 * The rules a blend space's sample run obeys while it is being authored, lifted clear of the
	 * window that drives them.
	 *
	 * They exist here rather than at the door because the alternative is a rig that will not upload:
	 * `validateBlendSet` and `AddRig` both refuse a run of fewer than two samples or one whose
	 * parameters do not strictly increase, and a break found there surfaces as a scene that fails to
	 * load rather than as a control that would not move. The panel cannot be tested at all --
	 * `RenderTargetWindow`'s constructor needs a real `winId()` -- so every rule it enforces is a
	 * free function here and pinned by a case, as GroundForSlope and FootIKForSliders already are.
	 *
	 * Every function takes the run in *authored* form: clips by name, in strictly increasing
	 * parameter order, which is what a `.bblend` stores and what is saved back.
	 *
	 * `precision` is the step the caller's box shows -- 0.01 for two decimals -- not a constant of
	 * the format. Two samples must be strictly apart because the span between them is what a weight
	 * divides by, and the smallest gap worth having is the smallest one a person can see and type: a
	 * rule holding the invariant to one ULP would read, in a box showing two decimals, as two
	 * samples at the same threshold.
	 *
	 * So "too close" is *rounds to the same displayed value*, an integer comparison, rather than a
	 * distance under `precision`. A distance cannot express it: the neighbouring step a person can
	 * actually type is a hair under `precision` away in binary -- 3.21f minus 3.2f is 0.0099999905
	 * -- so a subtraction refuses the very value the box was going to offer next.
	 */

	/**
	 * Where `parameter` belongs in `run`, which is in strictly increasing order.
	 *
	 * The index it would be inserted *before*, so `run.size()` means past the last sample.
	 */
	[[nodiscard]] size_t
	InsertionIndex(std::span<const assetlib::BlendSpaceSample> run, float parameter) noexcept;

	/**
	 * Whether a sample at `parameter` can join `run` and leave it a run a blend space accepts.
	 *
	 * False for a non-finite parameter, and for one that would display as a sample already there --
	 * which is the duplicate `validateBlendSet` refuses, caught while it is still a gesture.
	 */
	[[nodiscard]] bool
	CanInsertAt(
		std::span<const assetlib::BlendSpaceSample> run,
		float                                       parameter,
		float                                       precision) noexcept;

	/**
	 * `parameter` held where sample `index` may actually go: strictly between its neighbours, one
	 * displayed step clear of each. The ends are open, so the first and last samples move freely
	 * outward.
	 *
	 * Clamped rather than reordered. A run in a list is read top to bottom, and a row that jumped
	 * position mid-drag would move the thing under the cursor; this is the same choice the ground
	 * slope makes by committing on release rather than tracking.
	 *
	 * A non-finite `parameter` leaves the sample where it is. An `index` outside the run answers
	 * `parameter` unchanged -- there is nothing to hold it between.
	 */
	[[nodiscard]] float
	ClampedParameter(
		std::span<const assetlib::BlendSpaceSample> run,
		size_t                                      index,
		float                                       parameter,
		float                                       precision) noexcept;

	/**
	 * Whether `run` can give one sample up and still be a blend space.
	 *
	 * Two is the floor: a one-sample space is a clip, and every clip is already a node under its own
	 * name.
	 */
	[[nodiscard]] bool
	CanRemoveSample(std::span<const assetlib::BlendSpaceSample> run) noexcept;

	/**
	 * Whether `name` may name a space in a set that already holds `existing`.
	 *
	 * Unnamed and duplicate are both refused by `validateBlendSet`, and a set that cannot be saved
	 * is worse found at the save than at the keystroke.
	 */
	[[nodiscard]] bool
	CanNameSpace(std::span<const assetlib::BlendSpace> existing, std::string_view name) noexcept;

	/**
	 * The parameter a `Scrubber` at `tick` addresses, over a run spanning `min` to `max` in `ticks`
	 * steps.
	 *
	 * Scrubber is integer-valued on a closed range, and a parameter is not, so the cursor needs a
	 * map rather than a cast. `ticks` is the resolution the bar is given, not a property of the run.
	 *
	 * A degenerate span (`max` at or below `min`) answers `min`: a space whose samples have
	 * collapsed has one parameter to offer, and no cursor position means anything else.
	 */
	[[nodiscard]] float
	ParameterForTick(float min, float max, int ticks, int tick) noexcept;

	/**
	 * The tick nearest `parameter`, the inverse of ParameterForTick and clamped into `[0, ticks]`.
	 *
	 * Nearest rather than truncated, so a parameter typed into the box and the thumb that follows it
	 * do not disagree by a tick every time.
	 */
	[[nodiscard]] int
	TickForParameter(float min, float max, int ticks, float parameter) noexcept;

	/**
	 * Whether `after` differs from `before` in nothing but where its samples sit -- the same spaces
	 * under the same names, each holding the same samples naming the same clips.
	 *
	 * This is ADR-3's fork, and it is exactly the change `AssetManager::SetBlendParameters` accepts:
	 * true goes live on the rig already uploaded, false is a node table that has to be built again,
	 * so the mesh reloads. Asking it of the two authored sets rather than of the live spaces is what
	 * lets a *clip* swap be seen at all -- the live form holds indices, and a sample renamed onto
	 * another clip would otherwise read as no change until the rig refused it.
	 */
	[[nodiscard]] bool
	IsParameterMove(
		std::span<const assetlib::BlendSpace> before,
		std::span<const assetlib::BlendSpace> after) noexcept;

	/**
	 * Writes each authored parameter onto the matching live sample, leaving every clip index alone.
	 * False -- and nothing written -- unless the two correspond space for space and sample for
	 * sample, which is what `IsParameterMove` against the set the rig was acquired with establishes.
	 *
	 * The correspondence is positional, and it holds because `AssetManager::BlendSetFor` resolves a
	 * set in its own order and drops nothing: a clip it cannot find throws rather than skipping the
	 * sample. So `live[s].samples[i]` is `authored[s].samples[i]` resolved, and moving a threshold
	 * needs no second resolution of the names.
	 */
	[[nodiscard]] bool
	ApplyParameters(
		std::span<const assetlib::BlendSpace> authored,
		std::span<game::BlendSpaceInfo>       live) noexcept;

	/**
	 * Why `clip` cannot be a sample of a blend space, or empty when it can.
	 *
	 * A space sets one normalized phase shared by every sample, so a clip that clamps rather than
	 * wraps would sit on its last frame while the others cycle -- which is why `AddRig` refuses one.
	 * Offered here so the clip is greyed with the reason beside it (ADR-5) rather than accepted and
	 * then refused by a rig that will not upload.
	 */
	[[nodiscard]] std::string_view
	ClipRefusalReason(const ClipInfo& clip) noexcept;

	/** A run taken from measured speed, or the reason it could not be. Exactly one is set. */
	struct SpeedThresholds
	{
		std::vector<assetlib::BlendSpaceSample> run;
		std::string                             refusal;
	};

	/**
	 * `run` with every threshold taken from the speed its clip was animated at (ADR-7), re-sorted by
	 * those speeds.
	 *
	 * Re-sorted because thresholds from speed *are* an ordering by speed: a run authored walk-then-
	 * run but measured the other way round would otherwise stop strictly increasing, which is the
	 * one thing a blend space cannot be. So the measurement decides the order, not the authoring.
	 *
	 * Refused -- with `run` left empty -- when two clips were animated at the same speed, when one
	 * does not travel and another also does not, or when a sample names a clip `clips` does not
	 * hold. The first two are the same refusal: the span between two samples is what a weight
	 * divides by, so two at one threshold have no weighting between them.
	 */
	[[nodiscard]] SpeedThresholds
	ThresholdsFromSpeed(
		std::span<const assetlib::BlendSpaceSample> run,
		std::span<const ClipInfo>                   clips);

	/**
	 * The space `node` names, or null when it names a clip, nothing, or a space `spaces` lost.
	 *
	 * `node` is into the rig's node table -- every clip in clip order, then `spaces` -- so this is
	 * the one place the boundary between the two halves is decided. A second reading of it is a
	 * second chance to be off by one.
	 */
	[[nodiscard]] const game::BlendSpaceInfo*
	SpaceForNode(std::span<const game::BlendSpaceInfo> spaces, size_t clipCount, int node) noexcept;

	/**
	 * One sample interval of what node `node` plays at `parameter`, in Hz -- which is what an
	 * unblended fade meets over.
	 *
	 * A space has no rate of its own, so it is the rate of the lower of the two samples it straddles
	 * there -- the clip that is actually playing, rather than an average of a pair.
	 *
	 * Zero when `node` names nothing, when a space's sample names a clip `clips` does not hold, or
	 * when `clips` is empty. A caller reads that as "no cut is expressible", never as a rate.
	 */
	[[nodiscard]] float
	NodeSampleRate(
		std::span<const ClipInfo>             clips,
		std::span<const game::BlendSpaceInfo> spaces,
		int                                   node,
		float                                 parameter) noexcept;
}
