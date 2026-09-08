#include "Windows/GpuTiming/PassHistory.h"

#include "Windows/GpuTiming/pass_timing_csv.h"
#include <QString>
#include <QStringList>
#include <bgl/PassTiming.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <qcontainerfwd.h>
#include <string>
#include <utility>
#include <vector>

// The table behind the GPU timing graph, and its export. Every rule here exists because the frame
// graph does not run the same passes every frame: a culled pass leaves no row, and a pass appearing
// part way through must not shift the samples already recorded onto the wrong columns.

namespace
{
	[[nodiscard]] bgl::PassTimings
	Frame(uint64_t id, std::vector<bgl::PassTiming> passes)
	{
		return bgl::PassTimings{ .frame = id, .passes = std::move(passes) };
	}
}

TEST_CASE("A frame already recorded is not recorded twice", "[gputiming]")
{
	editor::PassHistory history;

	history.Append(Frame(7, { { .name = "Clear", .milliseconds = 1.0 } }));
	history.Append(Frame(7, { { .name = "Clear", .milliseconds = 1.0 } }));

	CHECK(history.SampleCount() == 1);

	history.Append(Frame(8, { { .name = "Clear", .milliseconds = 1.0 } }));
	CHECK(history.SampleCount() == 2);
}

TEST_CASE("A read that resolved no rows records nothing", "[gputiming]")
{
	editor::PassHistory history;

	history.Append(Frame(0, {}));

	CHECK(history.SampleCount() == 0);
	CHECK(history.PeakTotal() == 0.0);
}

TEST_CASE("The oldest samples fall off the end once the history is full", "[gputiming]")
{
	editor::PassHistory history(3);

	for (uint64_t frame = 1; frame <= 5; ++frame)
	{
		history.Append(
			Frame(frame, { { .name = "Clear", .milliseconds = static_cast<double>(frame) } }));
	}

	REQUIRE(history.SampleCount() == 3);
	CHECK(history.FrameAt(0) == 3);
	CHECK(history.FrameAt(2) == 5);
}

TEST_CASE("A pass that appears part way through lands in execution order", "[gputiming]")
{
	editor::PassHistory history;

	history.Append(Frame(
		1,
		{ { .name = "Clear", .milliseconds = 0.1 },
	      { .name = "Forward 0", .milliseconds = 4.0 },
	      { .name = "PreparePresent", .milliseconds = 0.2 } }));

	// TaaResolve switched on: it runs after Forward, and belongs beside it rather than at the end.
	history.Append(Frame(
		2,
		{ { .name = "Clear", .milliseconds = 0.1 },
	      { .name = "Forward 0", .milliseconds = 4.0 },
	      { .name = "TaaResolve", .milliseconds = 0.5 },
	      { .name = "PreparePresent", .milliseconds = 0.2 } }));

	const std::vector<std::string> passes(history.Passes().begin(), history.Passes().end());
	REQUIRE(passes.size() == 4);
	CHECK(passes[2] == "TaaResolve");
	CHECK(passes[3] == "PreparePresent");

	// And the frame recorded before it existed still reads on its own columns, not shifted by one.
	CHECK(history.At(0, 3).value() == 0.2);
	CHECK_FALSE(history.At(0, 2).has_value());
	CHECK(history.At(1, 2).value() == 0.5);
}

TEST_CASE("A frame's total is what its passes cost together", "[gputiming]")
{
	editor::PassHistory history;

	history.Append(Frame(
		1,
		{ { .name = "Clear", .milliseconds = 0.5 },
	      { .name = "Forward 0", .milliseconds = 4.0 } }));
	history.Append(Frame(
		2,
		{ { .name = "Clear", .milliseconds = 0.5 },
	      { .name = "Forward 0", .milliseconds = 12.0 } }));

	CHECK(history.TotalAt(0) == 4.5);
	CHECK(history.PeakTotal() == 12.5);
}

TEST_CASE("Clearing forgets the frames and the passes", "[gputiming]")
{
	editor::PassHistory history;

	history.Append(Frame(1, { { .name = "Clear", .milliseconds = 0.5 } }));
	history.Clear();

	CHECK(history.SampleCount() == 0);
	CHECK(history.Passes().empty());

	// The id guard goes with them: the same frame is a new recording after a clear, which is what a
	// viewport switching back to the one it was showing depends on.
	history.Append(Frame(1, { { .name = "Clear", .milliseconds = 0.5 } }));
	CHECK(history.SampleCount() == 1);
}

TEST_CASE("The CSV holds a header even with nothing recorded", "[gputiming]")
{
	const QString csv = editor::PassHistoryCsv(editor::PassHistory());

	CHECK(csv == "sample,frame,total\n");
}

TEST_CASE("The CSV lists one row per sample, oldest first, with a total", "[gputiming]")
{
	editor::PassHistory history;
	history.Append(Frame(
		11,
		{ { .name = "Clear", .milliseconds = 0.125 },
	      { .name = "Forward 0", .milliseconds = 4.5 } }));
	history.Append(Frame(
		12,
		{ { .name = "Clear", .milliseconds = 0.125 },
	      { .name = "Forward 0", .milliseconds = 1.0 } }));

	const QStringList lines = editor::PassHistoryCsv(history).split('\n');

	REQUIRE(lines.size() == 4);
	CHECK(lines[0] == "sample,frame,Clear,Forward 0,total");
	CHECK(lines[1] == "0,11,0.125,4.500,4.625");
	CHECK(lines[2] == "1,12,0.125,1.000,1.125");
	CHECK(lines[3].isEmpty());
}

TEST_CASE("A pass that did not run leaves the field empty, not zero", "[gputiming]")
{
	editor::PassHistory history;
	history.Append(Frame(1, { { .name = "Clear", .milliseconds = 0.125 } }));
	history.Append(Frame(
		2,
		{ { .name = "Clear", .milliseconds = 0.125 },
	      // Zero is a pass that ran and could not be sampled, and must survive as a figure.
	      { .name = "SceneUpdate 0", .milliseconds = 0.0 } }));

	const QStringList lines = editor::PassHistoryCsv(history).split('\n');

	REQUIRE(lines.size() >= 3);
	CHECK(lines[1] == "0,1,0.125,,0.125");
	CHECK(lines[2] == "1,2,0.125,0.000,0.125");
}

TEST_CASE("A pass name carrying a comma stays one field", "[gputiming]")
{
	editor::PassHistory history;
	history.Append(Frame(1, { { .name = "Forward, part 2", .milliseconds = 1.0 } }));

	const QStringList lines = editor::PassHistoryCsv(history).split('\n');

	CHECK(lines[0] == "sample,frame,\"Forward, part 2\",total");
}
