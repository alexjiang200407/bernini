#include "Windows/GpuTiming/pass_graph_paint.h"

#include <QBrush>
#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <Qt>
#include <algorithm>
#include <bgl/PassHistory.h>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace
{
	constexpr int    c_Margin      = 8;
	constexpr int    c_AxisWidth   = 52;
	constexpr int    c_AxisHeight  = 18;
	constexpr int    c_LegendWidth = 210;
	constexpr int    c_RowHeight   = 17;
	constexpr int    c_GridLines   = 4;
	constexpr double c_MinAxisMs   = 0.5;

	// A round number at or above `peak`, so the axis labels read 2.0 rather than 1.87 and a spike
	// does not rescale the graph on every frame.
	[[nodiscard]] double
	AxisCeiling(const double peak)
	{
		if (!(peak > c_MinAxisMs))
			return c_MinAxisMs;

		const double magnitude = std::pow(10.0, std::floor(std::log10(peak)));
		for (const double step : { 1.0, 2.0, 5.0 })
		{
			if (peak <= step * magnitude)
				return step * magnitude;
		}
		return 10.0 * magnitude;
	}

	[[nodiscard]] QString
	Milliseconds(const double value)
	{
		return QString::asprintf("%.3f ms", value);
	}

	// Where the bands are drawn inside the whole chart: the axis labels take a column on the left,
	// the legend one on the right, and the frame markers a strip below.
	[[nodiscard]] QRect
	PlotRect(const QRect& rect)
	{
		const int legendWidth = std::min(c_LegendWidth, rect.width() / 3);
		return QRect(
			rect.left() + c_Margin + c_AxisWidth,
			rect.top() + c_Margin,
			std::max(1, rect.width() - legendWidth - c_AxisWidth - 3 * c_Margin),
			std::max(1, rect.height() - c_AxisHeight - 2 * c_Margin));
	}

	[[nodiscard]] double
	SampleX(const QRect& plot, const std::size_t sample, const std::size_t samples)
	{
		const double span = static_cast<double>(samples > 1 ? samples - 1 : 1);
		return plot.left() + static_cast<double>(plot.width()) * static_cast<double>(sample) / span;
	}
}

namespace editor
{
	std::optional<std::size_t>
	PassGraphSampleAt(const QRect& rect, const bgl::PassHistory& history, const int x)
	{
		const std::size_t samples = history.SampleCount();
		const QRect       plot    = PlotRect(rect);
		if (samples == 0 || x < plot.left() || x > plot.right())
			return std::nullopt;

		const double span     = static_cast<double>(samples > 1 ? samples - 1 : 1);
		const double fraction = static_cast<double>(x - plot.left()) / plot.width();
		return std::min(samples - 1, static_cast<std::size_t>(std::lround(fraction * span)));
	}

	QColor
	PassBandColor(const std::size_t pass)
	{
		// Golden-angle hue walk: adjacent passes land far apart on the wheel, so neighbouring bands
		// are told apart without a table of colours to keep in step with the frame graph.
		const double hue = std::fmod(0.137 + 0.618033988749895 * static_cast<double>(pass), 1.0);
		return QColor::fromHsvF(hue, 0.52, 0.88);
	}

	void
	PaintPassGraph(
		QPainter&                        painter,
		const QRect&                     rect,
		const bgl::PassHistory&          history,
		const std::optional<std::size_t> selected,
		const QPalette&                  palette)
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.fillRect(rect, palette.base());

		const std::size_t samples = history.SampleCount();
		if (samples == 0)
		{
			painter.setPen(palette.color(QPalette::Disabled, QPalette::Text));
			painter.drawText(rect, Qt::AlignCenter, "No timed frames yet");
			painter.restore();
			return;
		}

		const QRect plot = PlotRect(rect);

		const double axisMax = AxisCeiling(history.PeakTotal());
		const auto   xOf     = [&plot, samples](const std::size_t sample) {
			return SampleX(plot, sample, samples);
		};
		const auto yOf = [&plot, axisMax](const double milliseconds) {
			return plot.bottom() - static_cast<double>(plot.height()) * milliseconds / axisMax;
		};

		for (int line = 0; line <= c_GridLines; ++line)
		{
			const double value = axisMax * line / c_GridLines;
			const double y     = yOf(value);

			painter.setPen(QPen(palette.color(QPalette::Mid), 1.0, Qt::DotLine));
			painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));

			painter.setPen(palette.color(QPalette::Text));
			painter.drawText(
				QRectF(rect.left(), y - c_RowHeight / 2.0, c_AxisWidth, c_RowHeight),
				Qt::AlignRight | Qt::AlignVCenter,
				QString::asprintf("%.2f", value));
		}

		painter.drawText(
			QRect(rect.left(), rect.top(), c_AxisWidth, c_RowHeight),
			Qt::AlignRight | Qt::AlignBottom,
			"ms");

		// Each band is drawn on top of the running total of the bands below it, which is what makes
		// the outline of the stack the frame's whole GPU cost.
		std::vector<double> below(samples, 0.0);
		for (std::size_t pass = 0; pass < history.Passes().size(); ++pass)
		{
			QPolygonF band;
			band.reserve(static_cast<int>(2 * samples));

			for (std::size_t sample = 0; sample < samples; ++sample)
			{
				const double top = below[sample] + history.At(sample, pass).value_or(0.0);
				band.append(QPointF(xOf(sample), yOf(top)));
				below[sample] = top;
			}
			for (std::size_t sample = samples; sample-- > 0;)
			{
				band.append(QPointF(
					xOf(sample),
					yOf(below[sample] - history.At(sample, pass).value_or(0.0))));
			}

			painter.setPen(Qt::NoPen);
			painter.setBrush(PassBandColor(pass));
			painter.drawPolygon(band);
		}

		const std::size_t marked =
			selected.has_value() && *selected < samples ? *selected : samples - 1;

		painter.setPen(QPen(palette.color(QPalette::Highlight), 1.0));
		painter.drawLine(QPointF(xOf(marked), plot.top()), QPointF(xOf(marked), plot.bottom()));

		painter.setPen(palette.color(QPalette::Text));
		painter.drawText(
			QRect(plot.left(), plot.bottom() + 2, plot.width(), c_AxisHeight),
			Qt::AlignLeft | Qt::AlignVCenter,
			QString("oldest of %1 frames").arg(samples));
		painter.drawText(
			QRect(plot.left(), plot.bottom() + 2, plot.width(), c_AxisHeight),
			Qt::AlignRight | Qt::AlignVCenter,
			"newest");

		// The legend is the breakdown of the marked frame: the same rows the log table carries, in
		// the colours the bands above are drawn in.
		QRect row(
			plot.right() + c_Margin,
			rect.top() + c_Margin,
			rect.right() - plot.right() - 2 * c_Margin,
			c_RowHeight);

		painter.setPen(palette.color(QPalette::Text));
		painter.drawText(
			row,
			Qt::AlignLeft | Qt::AlignVCenter,
			QString("frame %1 — %2")
				.arg(history.FrameAt(marked))
				.arg(Milliseconds(history.TotalAt(marked))));
		row.translate(0, c_RowHeight);

		const QFontMetrics metrics = painter.fontMetrics();
		for (std::size_t pass = 0; pass < history.Passes().size(); ++pass)
		{
			if (row.bottom() > rect.bottom())
				break;

			const std::optional<double> cost = history.At(marked, pass);

			painter.fillRect(QRect(row.left(), row.top() + 4, 9, 9), PassBandColor(pass));

			const QRect label = row.adjusted(14, 0, -62, 0);
			painter.setPen(palette.color(
				cost.has_value() ? QPalette::Active : QPalette::Disabled,
				QPalette::Text));
			painter.drawText(
				label,
				Qt::AlignLeft | Qt::AlignVCenter,
				metrics.elidedText(
					QString::fromStdString(history.Passes()[pass]),
					Qt::ElideRight,
					label.width()));
			painter.drawText(
				row,
				Qt::AlignRight | Qt::AlignVCenter,
				cost.has_value() ? Milliseconds(*cost) : QString("—"));

			row.translate(0, c_RowHeight);
		}

		painter.restore();
	}
}
