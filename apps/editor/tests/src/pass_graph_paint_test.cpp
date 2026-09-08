#include "Windows/GpuTiming/pass_graph_paint.h"

#include "Windows/GpuTiming/PassHistory.h"
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QSvgGenerator>
#include <QSvgRenderer>
#include <bgl/PassTiming.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <qnamespace.h>
#include <qrgb.h>
#include <set>

// The chart is a picture, so what a case can pin is that it drew: the bands are there, they are
// told apart, and the sample marked is the one asked for. The `.got.png` beside the goldens is what
// a person -- or an agent -- looks at to judge the rest, and is the same call the export writes.

namespace
{
	constexpr auto c_Got    = "assets/golden/gpu_timing_graph.got.png";
	constexpr auto c_GotSvg = "assets/golden/gpu_timing_graph.got.svg";

	// A frame whose Forward cost swings, so the picture has a spike in it to read.
	[[nodiscard]] bgl::PassTimings
	SyntheticFrame(const uint64_t id)
	{
		const double phase   = static_cast<double>(id) * 0.11;
		const double forward = 3.0 + 1.4 * std::sin(phase) + (id == 70 ? 9.0 : 0.0);

		return bgl::PassTimings{ .frame  = id,
			                     .passes = { { .name = "Clear", .milliseconds = 0.08 },
			                                 { .name = "SceneUpdate 0", .milliseconds = 0.31 },
			                                 { .name         = "Skinning",
			                                   .milliseconds = 0.9 + 0.2 * std::cos(phase) },
			                                 { .name = "Forward 0", .milliseconds = forward },
			                                 { .name = "TaaResolve", .milliseconds = 0.62 },
			                                 { .name = "PostProcess", .milliseconds = 0.44 },
			                                 { .name = "PreparePresent", .milliseconds = 0.05 } } };
	}

	[[nodiscard]] std::size_t
	DistinctColours(const QImage& image)
	{
		std::set<QRgb> colours;
		for (int y = 0; y < image.height(); ++y)
		{
			for (int x = 0; x < image.width(); ++x)
			{
				colours.insert(image.pixel(x, y));
			}
		}
		return colours.size();
	}

	// A band is a saturated colour; the ground, the grid and the text are greys, whatever the
	// platform's palette and however the font is antialiased.
	[[nodiscard]] bool
	HasBandColour(const QImage& image)
	{
		for (int y = 0; y < image.height(); ++y)
		{
			for (int x = 0; x < image.width(); ++x)
			{
				if (QColor(image.pixel(x, y)).hsvSaturation() > 64)
					return true;
			}
		}
		return false;
	}

	[[nodiscard]] QImage
	Render(const editor::PassHistory& history, const std::optional<std::size_t> selected)
	{
		QImage image(900, 380, QImage::Format_ARGB32);
		image.fill(Qt::transparent);

		QPainter painter(&image);
		editor::PaintPassGraph(painter, image.rect(), history, selected, QPalette());
		painter.end();

		return image;
	}
}

TEST_CASE("A history of frames draws its bands", "[gputiming]")
{
	editor::PassHistory history;
	for (uint64_t frame = 1; frame <= 120; ++frame)
	{
		history.Append(SyntheticFrame(frame));
	}

	const QImage image = Render(history, std::nullopt);
	REQUIRE(image.save(c_Got));

	// Seven passes, each its own band, plus the ground and the labels: a blank chart, or one that
	// drew every band in the same colour, could not reach this.
	CHECK(DistinctColours(image) > 7);
	CHECK(HasBandColour(image));
}

// What the export writes is the same call painting a vector device, so the drawing zooms instead of
// blurring. Rendered back here because QSvgGenerator reports nothing at all: a file that parses and
// draws the bands is the only evidence the export produced a chart rather than an empty canvas.
TEST_CASE("The drawing the export writes is vector, and draws the same chart", "[gputiming]")
{
	editor::PassHistory history;
	for (uint64_t frame = 1; frame <= 120; ++frame)
	{
		history.Append(SyntheticFrame(frame));
	}

	const QRect frame(0, 0, 900, 380);
	{
		QSvgGenerator drawing;
		drawing.setFileName(c_GotSvg);
		drawing.setSize(frame.size());
		drawing.setViewBox(frame);

		QPainter painter(&drawing);
		editor::PaintPassGraph(painter, frame, history, std::nullopt, QPalette());
	}

	const QString path = QString::fromUtf8(c_GotSvg);
	QSvgRenderer  rendered(path);
	REQUIRE(rendered.isValid());

	QImage raster(frame.size(), QImage::Format_ARGB32);
	raster.fill(Qt::transparent);
	{
		QPainter painter(&raster);
		rendered.render(&painter);
	}

	CHECK(HasBandColour(raster));
}

TEST_CASE("An empty history says so rather than drawing an empty chart", "[gputiming]")
{
	const QImage image = Render(editor::PassHistory(), std::nullopt);

	// It drew something, and none of it is a band.
	CHECK(DistinctColours(image) > 1);
	CHECK_FALSE(HasBandColour(image));
}

TEST_CASE("The marked sample is the one the caller asked for", "[gputiming]")
{
	editor::PassHistory history;
	for (uint64_t frame = 1; frame <= 40; ++frame)
	{
		history.Append(SyntheticFrame(frame));
	}

	const QImage latest   = Render(history, std::nullopt);
	const QImage middle   = Render(history, 12);
	const QImage repeated = Render(history, 12);

	CHECK(middle != latest);
	CHECK(middle == repeated);

	// A selection that has scrolled out of the history falls back to the newest frame rather than
	// marking whichever sample now sits at that index.
	CHECK(Render(history, 999) == latest);
}
