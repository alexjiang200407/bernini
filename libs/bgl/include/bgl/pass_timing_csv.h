#pragma once
#include <bgl/PassHistory.h>
#include <cstddef>
#include <format>
#include <optional>
#include <string>

namespace bgl
{
	namespace detail
	{
		// A pass is named by the frame graph, so a comma or a quote in one is unlikely rather than
		// impossible, and a CSV that only mostly parses is worse than one that always does.
		[[nodiscard]] inline std::string
		CsvField(const std::string& text)
		{
			if (text.find_first_of(",\"") == std::string::npos)
				return text;

			std::string quoted = "\"";
			for (const char c : text)
			{
				if (c == '"')
					quoted += '"';
				quoted += c;
			}
			return quoted + '"';
		}
	}

	/**
	 * The history as CSV: a `sample,frame,<pass>…,total` header, then one row per sample oldest
	 * first, milliseconds to three decimals. The sample index is the position in the run and the
	 * frame is the engine's own id, so a row can be found by either.
	 *
	 * A pass that did not run in a frame is an **empty** field rather than a zero: zero is what a
	 * pass that ran and could not be sampled reports.
	 *
	 * @return the header alone when there are no samples, so an export always says what it holds.
	 */
	[[nodiscard]] inline std::string
	PassHistoryCsv(const PassHistory& history)
	{
		std::string csv = "sample,frame";
		for (const std::string& pass : history.Passes())
		{
			csv += ',' + detail::CsvField(pass);
		}
		csv += ",total\n";

		for (std::size_t sample = 0; sample < history.SampleCount(); ++sample)
		{
			csv += std::format("{},{}", sample, history.FrameAt(sample));

			for (std::size_t pass = 0; pass < history.Passes().size(); ++pass)
			{
				const std::optional<double> cell = history.At(sample, pass);
				csv += ',';
				if (cell.has_value())
					csv += std::format("{:.3f}", *cell);
			}

			csv += std::format(",{:.3f}\n", history.TotalAt(sample));
		}

		return csv;
	}
}
