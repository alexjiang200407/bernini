#pragma once

#include <QColor>
#include <cstddef>
#include <optional>

class QPainter;
class QPalette;
class QRect;

namespace bgl
{
	class PassHistory;
}

namespace editor
{
	/** The band a pass is drawn in. Keyed by column, so a pass keeps its colour for the run. */
	[[nodiscard]] QColor
	PassBandColor(std::size_t pass);

	/**
	 * Which sample the point `x` falls on, for a graph drawn over `rect` -- the inverse of the
	 * mapping PaintPassGraph draws with, and here so that there is one of it.
	 *
	 * @return nullopt outside the plotted area, and when there is nothing recorded.
	 */
	[[nodiscard]] std::optional<std::size_t>
	PassGraphSampleAt(const QRect& rect, const bgl::PassHistory& history, int x);

	/**
	 * Draws `history` over `rect` as a stacked area, oldest sample at the left: one band per pass in
	 * execution order, so the top of the stack is what the frame cost on the GPU and a bulge names
	 * the band that caused it. A legend down the right lists the passes with what each cost in the
	 * sample marked, and the sample marked is where the graph is read.
	 *
	 * The same call paints the window and the exported image, so the file is the picture on screen
	 * rather than a second drawing of the same numbers. Every colour comes from `palette`, which is
	 * why it is passed rather than taken from the widget: the export has no widget.
	 *
	 * @param selected The sample to mark and to list, or nullopt for the newest one. Out of range is
	 *                 treated as nullopt rather than clamped -- a stale selection describes a frame
	 *                 that has scrolled away, and pointing at its neighbour would be a lie.
	 */
	void
	PaintPassGraph(
		QPainter&                  painter,
		const QRect&               rect,
		const bgl::PassHistory&    history,
		std::optional<std::size_t> selected,
		const QPalette&            palette);
}
