#pragma once

#include <QString>

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
}
