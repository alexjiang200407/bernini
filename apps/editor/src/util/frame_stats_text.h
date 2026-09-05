#pragma once

#include <QString>
#include <bgl/PassTiming.h>
#include <optional>
#include <vector>

namespace editor
{
	/** One viewport's frame timings, as the status bar shows them. */
	struct FrameStats
	{
		double meanMs = 0.0;
		double maxMs  = 0.0;
		int    missed = 0;
	};

	/**
	 * The status bar's frame-time readout for the viewport named `viewport`.
	 *
	 * @param stats nullopt before the viewport has reported, which is not the same as reporting
	 *              zeroes -- a viewport that has just become visible has measured nothing yet.
	 */
	[[nodiscard]] QString
	FrameStatsText(const QString& viewport, const std::optional<FrameStats>& stats);

	/**
	 * The per-pass GPU breakdown: one line per pass in the order the frame ran them, its
	 * milliseconds, and their total, in columns that line up in a fixed-width face. Plain text, so
	 * the same table goes under the readout's tooltip and into the log.
	 *
	 * @return empty when there are no rows -- timing off, or no timed frame has landed yet -- so the
	 *         tooltip is the plain one rather than a table with nothing in it.
	 */
	[[nodiscard]] QString
	PassTimingsText(const std::vector<bgl::PassTiming>& rows);
}
