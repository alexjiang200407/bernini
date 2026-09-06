#include "util/frame_stats_text.h"

#include <bgl/PassTiming.h>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <qobject.h>
#include <vector>

// The status bar's readout, pinned without a window or a device. What matters here is that "has not
// measured yet" and "measured zero" are different strings: the bug this replaced showed a viewport's
// last figures after it had stopped rendering, so a viewport that has reported nothing must say so
// rather than show numbers.

TEST_CASE("A viewport that has not reported yet says so", "[framestats]")
{
	const QString text = editor::FrameStatsText("Material Editor", std::nullopt);

	CHECK(text.contains("Material Editor"));
	CHECK(text.contains("measuring"));

	// The distinguishing property: no figures at all, not zeroed ones.
	CHECK_FALSE(text.contains("ms"));
	CHECK_FALSE(text.contains("missed"));
}

TEST_CASE("A reported frame shows its three figures under the viewport's name", "[framestats]")
{
	// Deliberately off the .x5 boundary: Qt::asprintf does its own double formatting and rounds
	// half away from zero where libc rounds half to even, and this pins the readout, not that.
	const auto    stats = editor::FrameStats{ .meanMs = 3.4, .maxMs = 12.5, .missed = 7 };
	const QString text  = editor::FrameStatsText("Level Editor", stats);

	CHECK(text.contains("Level Editor"));
	CHECK(text.contains("3.4 ms"));
	CHECK(text.contains("12.5 ms"));
	CHECK(text.contains("7 missed"));
	CHECK_FALSE(text.contains("measuring"));
}

TEST_CASE("Zeroed stats are reported, not mistaken for having measured nothing", "[framestats]")
{
	const auto    stats = editor::FrameStats{};
	const QString text  = editor::FrameStatsText("Animation Editor", stats);

	CHECK(text.contains("0.0 ms"));
	CHECK(text.contains("0 missed"));
	CHECK_FALSE(text.contains("measuring"));
}

TEST_CASE("The viewport's name is what distinguishes one readout from another", "[framestats]")
{
	const auto stats = editor::FrameStats{ .meanMs = 1.0, .maxMs = 2.0, .missed = 0 };

	CHECK(
		editor::FrameStatsText("Level Editor", stats) !=
		editor::FrameStatsText("Material Editor", stats));
}

// The tooltip's per-pass table: every pass by name in the order given, its cost, and a total, or
// nothing at all when there are no rows -- an empty table would read as a frame that ran nothing.

TEST_CASE("No pass rows means no breakdown at all", "[framestats]")
{
	CHECK(editor::PassTimingsText({}).isEmpty());
}

TEST_CASE("The breakdown lists every pass in order with its cost and their total", "[framestats]")
{
	const std::vector<bgl::PassTiming> rows{ { .name = "Clear", .milliseconds = 0.125 },
		                                     { .name = "Forward 0", .milliseconds = 4.5 },
		                                     { .name = "PostProcess", .milliseconds = 0.375 } };

	const QString text = editor::PassTimingsText(rows);

	CHECK(text.indexOf("Clear") < text.indexOf("Forward 0"));
	CHECK(text.indexOf("Forward 0") < text.indexOf("PostProcess"));
	CHECK(text.contains("0.125 ms"));
	CHECK(text.contains("4.500 ms"));
	CHECK(text.contains("0.375 ms"));
	CHECK(text.contains("total"));
	CHECK(text.contains("5.000 ms"));
}

TEST_CASE("A pass the GPU could not sample is listed at zero rather than dropped", "[framestats]")
{
	const std::vector<bgl::PassTiming> rows{ { .name = "Scene Update 0", .milliseconds = 0.0 },
		                                     { .name = "Forward 0", .milliseconds = 1.0 } };

	const QString text = editor::PassTimingsText(rows);

	CHECK(text.contains("Scene Update 0"));
	CHECK(text.contains("0.000 ms"));
}
