#pragma once

#include <QString>

namespace editor
{
	class PassHistory;

	/**
	 * The history as CSV: a `sample,frame,<pass>…,total` header, then one row per sample oldest
	 * first, milliseconds to three decimals. The sample index is the graph's x axis and the frame is
	 * the engine's own id, so a row can be found in either.
	 *
	 * A pass that did not run in a frame is an **empty** field rather than a zero: zero is what a
	 * pass that ran and could not be sampled reports.
	 *
	 * @return the header alone when there are no samples, so an export always says what it holds.
	 */
	[[nodiscard]] QString
	PassHistoryCsv(const PassHistory& history);
}
