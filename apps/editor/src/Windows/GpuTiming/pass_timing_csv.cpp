#include "Windows/GpuTiming/pass_timing_csv.h"

#include "Windows/GpuTiming/PassHistory.h"
#include <QString>
#include <cstddef>
#include <optional>
#include <string>

namespace
{
	// A pass is named by the frame graph, so a comma or a quote in one is unlikely rather than
	// impossible, and a CSV that only mostly parses is worse than one that always does.
	[[nodiscard]] QString
	Field(const std::string& text)
	{
		QString field = QString::fromStdString(text);
		if (!field.contains(',') && !field.contains('"'))
			return field;

		return '"' + field.replace('"', "\"\"") + '"';
	}
}

namespace editor
{
	QString
	PassHistoryCsv(const PassHistory& history)
	{
		QString csv = "sample,frame";
		for (const std::string& pass : history.Passes())
		{
			csv += ',' + Field(pass);
		}
		csv += ",total\n";

		for (std::size_t sample = 0; sample < history.SampleCount(); ++sample)
		{
			csv += QString::number(sample) + ',' + QString::number(history.FrameAt(sample));

			for (std::size_t pass = 0; pass < history.Passes().size(); ++pass)
			{
				const std::optional<double> cell = history.At(sample, pass);
				csv += ',';
				if (cell.has_value())
					csv += QString::asprintf("%.3f", *cell);
			}

			csv += QString::asprintf(",%.3f\n", history.TotalAt(sample));
		}

		return csv;
	}
}
