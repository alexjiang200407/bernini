#include "Windows/GpuTiming/pass_graph_paint.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <algorithm>
#include <array>
#include <bgl/PassHistory.h>
#include <bgl/PassTiming.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <qnamespace.h>
#include <qrgb.h>
#include <qtypes.h>
#include <set>
#include <string>

// The chart is a picture, so what a case can pin is that it drew: the bands are there, they are
// told apart, and the sample marked is the one asked for. The `.got.png` beside the goldens is what
// a person -- or an agent -- looks at to judge the rest, and is the same call the export writes.

namespace
{
	constexpr auto c_Got = "assets/golden/gpu_timing_graph.got.png";

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
	Render(const bgl::PassHistory& history, const std::optional<std::size_t> selected)
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
	bgl::PassHistory history;
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

TEST_CASE("An empty history says so rather than drawing an empty chart", "[gputiming]")
{
	const QImage image = Render(bgl::PassHistory(), std::nullopt);

	// It drew something, and none of it is a band.
	CHECK(DistinctColours(image) > 1);
	CHECK_FALSE(HasBandColour(image));
}

TEST_CASE("The marked sample is the one the caller asked for", "[gputiming]")
{
	bgl::PassHistory history;
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

TEST_CASE("Dense timing bands join without gaps and retain a one-frame spike", "[gputiming]")
{
	bgl::PassHistory history;
	for (uint64_t frame = 1; frame <= 600; ++frame)
	{
		history.Append(
			{ .frame  = frame,
		      .passes = { { .name = "Base", .milliseconds = 1.0 },
		                  { .name = "Spike", .milliseconds = frame == 301 ? 1.0 : 0.0 } } });
	}
	const QImage image      = Render(history, std::nullopt);
	const QRgb   base       = editor::PassBandColor(0).rgb();
	const QRgb   spike      = editor::PassBandColor(1).rgb();
	bool         foundSpike = false;
	for (int x = 0; x < image.width(); ++x)
	{
		const auto sample = editor::PassGraphSampleAt(image.rect(), history, x);
		if (!sample || *sample == 0 || *sample >= history.SampleCount() - 2)
			continue;
		CAPTURE(x);
		CHECK(image.pixel(x, 300) == base);
		if (image.pixel(x, 160) == spike)
		{
			CHECK(*sample >= 299);
			CHECK(*sample <= 301);
			foundSpike = true;
		}
	}
	CHECK(foundSpike);
}

TEST_CASE("Noisy GPU timings stay affordable relative to flat timings", "[gputiming][perf]")
{
	const auto historyOf = [](bool noisy) {
		bgl::PassHistory history;
		for (uint64_t frame = 1; frame <= 600; ++frame)
		{
			bgl::PassTimings timings{ .frame = frame };
			for (uint64_t pass = 0; pass < 24; ++pass)
			{
				const double cost =
					noisy ? 0.1 + static_cast<double>((frame * 73 + pass * 19) % 101) / 100.0 : 0.6;
				timings.passes.push_back({ .name = std::to_string(pass), .milliseconds = cost });
			}
			history.Append(timings);
		}
		return history;
	};
	const auto flat      = historyOf(false);
	const auto noisy     = historyOf(true);
	const auto paintTime = [](const bgl::PassHistory& history) {
		std::array<qint64, 5> times;
		for (qint64& elapsed : times)
		{
			QElapsedTimer clock;
			clock.start();
			const QImage image = Render(history, std::nullopt);
			elapsed            = clock.nsecsElapsed();
			REQUIRE_FALSE(image.isNull());
		}
		std::ranges::sort(times);
		return times[times.size() / 2];
	};
	const QImage warmFlat  = Render(flat, std::nullopt);
	const QImage warmNoisy = Render(noisy, std::nullopt);
	const auto   flatNs    = paintTime(flat);
	const auto   noisyNs   = paintTime(noisy);
	INFO("Flat: " << flatNs << " ns; noisy: " << noisyNs << " ns");
	// Equal samples and passes: spikes must not multiply the cost of inspecting a slow frame.
	CHECK(noisyNs < 8 * flatNs);
}
