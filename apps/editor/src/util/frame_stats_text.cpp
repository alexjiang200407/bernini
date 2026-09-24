#include "util/frame_stats_text.h"
#include "util/editor_language.h"
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
			return Localize("editor.util.frame_stats_measuring", { viewport }, "{0} — measuring…");

		return Localize(
			"editor.util.frame_stats_reported",
			{ viewport, stats->meanMs, stats->maxMs, stats->slowFrames },
			"{0} — frame {1:.1f} ms avg  {2:.1f} ms max  {3} over 20 ms");
	}

	QString
	PassTimingsText(const std::vector<bgl::PassTiming>& rows)
	{
		if (rows.empty())
			return {};

		std::size_t width = 0;
		for (const bgl::PassTiming& row : rows) width = std::max(width, row.name.size());

		QString text;
		double  total = 0.0;
		for (const bgl::PassTiming& row : rows)
		{
			text += QString::fromStdString(row.name).leftJustified(static_cast<int>(width) + 2) +
			        QString::asprintf("%8.3f ms\n", row.milliseconds);
			total += row.milliseconds;
		}
		text += QString("total").leftJustified(static_cast<int>(width) + 2) +
		        QString::asprintf("%8.3f ms", total);
		return text;
	}
}
