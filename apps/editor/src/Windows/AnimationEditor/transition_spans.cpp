#include "transition_spans.h"

#include <algorithm>
#include <cmath>

namespace editor
{
	namespace
	{
		/** Where `seconds` falls across a strip `width` wide, clamped into it. */
		int
		XFor(const TransitionLayout& layout, const float seconds, const int width) noexcept
		{
			const float span = layout.windowEnd - layout.windowStart;
			const float at   = (seconds - layout.windowStart) / span;
			return std::clamp(
				static_cast<int>(std::lround(at * static_cast<float>(width))),
				0,
				width);
		}

		/** The bar between two clocks, which is empty rather than inverted when they cross. */
		BarSpan
		Between(
			const TransitionLayout& layout,
			const float             from,
			const float             to,
			const int               width) noexcept
		{
			const int left  = XFor(layout, from, width);
			const int right = XFor(layout, to, width);
			return BarSpan{ left, std::max(0, right - left) };
		}
	}

	TransitionSpans
	SpansForTransition(const TransitionLayout& layout, const int width) noexcept
	{
		// A window of no width has no time-to-x map at all, and every division below is by its
		// span. Nothing drawn beats a picture computed from an infinity.
		if (width <= 0 || !(layout.windowEnd > layout.windowStart))
			return TransitionSpans();

		const float fadeEnd = layout.start + std::max(0.0f, layout.duration);

		auto spans      = TransitionSpans();
		spans.from      = Between(layout, layout.windowStart, fadeEnd, width);
		spans.to        = Between(layout, layout.start, layout.windowEnd, width);
		spans.overlap   = Between(layout, layout.start, fadeEnd, width);
		spans.playheadX = XFor(layout, layout.time, width);
		return spans;
	}

	TransitionLayout
	WindowFor(const float start, const float duration, const float lead, const float tail) noexcept
	{
		const float fade = std::max(0.0f, duration);

		auto layout        = TransitionLayout();
		layout.windowStart = start - std::max(0.0f, lead);
		layout.windowEnd   = start + fade + std::max(0.0f, tail);
		layout.start       = start;
		layout.duration    = fade;
		layout.time        = layout.windowStart;

		// An instant fade with no lead and no tail would leave the two ends equal, which
		// SpansForTransition reads as nothing to draw and PlaybackTransport refuses outright.
		if (!(layout.windowEnd > layout.windowStart))
			layout.windowEnd = layout.windowStart + 1.0f;

		return layout;
	}
}
