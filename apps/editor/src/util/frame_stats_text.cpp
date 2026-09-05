#include "util/frame_stats_text.h"
#include <algorithm>
#include <bgl/PassTiming.h>
#include <cstddef>
#include <optional>
#include <qobject.h>
#include <vector>

namespace editor
{
	QString
	FrameStatsText(const QString& viewport, const std::optional<FrameStats>& stats)
	{
		if (!stats.has_value())
			return viewport + " — measuring…";

		return viewport + QString::asprintf(
							  " — frame %.1f ms avg  %.1f ms max  %d missed",
							  stats->meanMs,
							  stats->maxMs,
							  stats->missed);
	}

	QString
	PassTimingsText(const std::vector<bgl::PassTiming>& rows)
	{
		if (rows.empty())
			return {};

		std::size_t width = 0;
		for (const bgl::PassTiming& row : rows) width = std::max(width, row.name.size());

		QString text  = "<pre>";
		double  total = 0.0;
		for (const bgl::PassTiming& row : rows)
		{
			text += QString::fromStdString(row.name)
			            .leftJustified(static_cast<int>(width) + 2)
			            .toHtmlEscaped() +
			        QString::asprintf("%8.3f ms\n", row.milliseconds);
			total += row.milliseconds;
		}
		text += QString("total").leftJustified(static_cast<int>(width) + 2) +
		        QString::asprintf("%8.3f ms", total);
		text += "</pre>";
		return text;
	}
}
