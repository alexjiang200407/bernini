#include "util/frame_stats_text.h"
#include <optional>
#include <qobject.h>

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
}
