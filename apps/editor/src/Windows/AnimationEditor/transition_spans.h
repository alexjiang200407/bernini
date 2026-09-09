#pragma once

namespace editor
{
	/** A transition as the strip draws it: two clips, a fade of `duration` beginning at `start`. */
	struct TransitionLayout
	{
		float windowStart = 0.0f;
		float windowEnd   = 1.0f;
		float start       = 0.0f;
		float duration    = 0.0f;
		float time        = 0.0f;
	};

	/** One bar's horizontal extent, in pixels from the left edge. */
	struct BarSpan
	{
		int x     = 0;
		int width = 0;
	};

	/**
	 * Where the two clip bars, the fade between them and the playhead sit in a strip `width` wide.
	 *
	 * The window is the timeline, so all four are one time-to-x map and none of them is a
	 * separately-maintained opinion about the same fade.
	 */
	struct TransitionSpans
	{
		BarSpan from;
		BarSpan to;
		BarSpan overlap;
		int     playheadX = 0;
	};

	/**
	 * `layout` laid out across `width` pixels.
	 *
	 * Every span is read off what `game::CrossfadeTo` writes rather than chosen to look right: the
	 * outgoing clip is already playing when the window opens and its ramp reaches zero at
	 * `start + duration`; the incoming one begins at `start`, where its slot is given `tRef`; and
	 * the overlap between them *is* the duration, which is what lets one control answer which two
	 * clips, how long, and show me. A picture that disagreed with the record would be worse than
	 * none, since the whole point is judging a fade by eye.
	 *
	 * Spans are clamped into the strip and never inverted. A degenerate window or a non-positive
	 * width gives every span zero, which paints as nothing rather than as a wrong picture.
	 *
	 * One overlap is the whole rig crossing at once, which is what a fade currently is: a slot's
	 * weight applies to every bone. A bone mask (`ROADMAP.md` § Skinned Meshes, unticked) would end
	 * that -- an upper body could cross while the legs did not, and one bar per clip could no
	 * longer say when. The strip would need a row per masked group, not a wider overlap.
	 */
	[[nodiscard]] TransitionSpans
	SpansForTransition(const TransitionLayout& layout, int width) noexcept;

	/**
	 * The window that brackets a fade of `duration` at `start`: `lead` before it and `tail` after.
	 *
	 * The strip's decision rather than the transport's, which takes absolute bounds and holds no
	 * opinion about what a fade wants around it -- what has already been decided has to be visible
	 * going in, and what settles coming out.
	 */
	[[nodiscard]] TransitionLayout
	WindowFor(float start, float duration, float lead, float tail) noexcept;

	/**
	 * One clip filling the whole strip, with the playhead at `timeSeconds`: the same widget with
	 * nothing to fade to.
	 *
	 * A transition with its second end pushed to the far edge, rather than a mode of its own -- the
	 * To bar and the overlap both come out empty, which is what a single clip *is*. So the strip
	 * needs no notion of how many clips it is drawing, and the Clip tab and the Blend tab are the
	 * same timeline showing different records.
	 */
	[[nodiscard]] TransitionLayout
	WindowForClip(float periodSeconds, float timeSeconds) noexcept;
}
