#include <bgl/PassHistory.h>

#include <bgl/PassTiming.h>
#include <bgl/pass_timing_csv.h>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The table a caller polling GetPassTimings accumulates, and its export. Every rule here exists
// because the frame graph does not run the same passes every frame: a culled pass leaves no row, and
// a pass appearing part way through must not shift the samples already recorded onto the wrong
// columns.

namespace
{
	[[nodiscard]] bgl::PassTimings
	Frame(uint64_t id, std::vector<bgl::PassTiming> passes)
	{
		return bgl::PassTimings{ .frame = id, .passes = std::move(passes) };
	}

	[[nodiscard]] std::vector<std::string>
	Lines(const std::string& text)
	{
		std::vector<std::string> lines;
		std::size_t              start = 0;
		for (std::size_t at = text.find('\n'); at != std::string::npos; at = text.find('\n', start))
		{
			lines.emplace_back(text.substr(start, at - start));
			start = at + 1;
		}
		lines.emplace_back(text.substr(start));
		return lines;
	}
}

TEST_CASE("A frame already recorded is not recorded twice", "[passhistory]")
{
	bgl::PassHistory history;

	history.Append(Frame(7, { { .name = "Clear", .milliseconds = 1.0 } }));
	history.Append(Frame(7, { { .name = "Clear", .milliseconds = 1.0 } }));

	CHECK(history.SampleCount() == 1);

	history.Append(Frame(8, { { .name = "Clear", .milliseconds = 1.0 } }));
	CHECK(history.SampleCount() == 2);
}

TEST_CASE("A read that resolved no rows records nothing", "[passhistory]")
{
	bgl::PassHistory history;

	history.Append(Frame(0, {}));

	CHECK(history.SampleCount() == 0);
	CHECK(history.PeakTotal() == 0.0);
}

TEST_CASE("The oldest samples fall off the end once the history is full", "[passhistory]")
{
	bgl::PassHistory history(3);

	for (uint64_t frame = 1; frame <= 5; ++frame)
	{
		history.Append(
			Frame(frame, { { .name = "Clear", .milliseconds = static_cast<double>(frame) } }));
	}

	REQUIRE(history.SampleCount() == 3);
	CHECK(history.FrameAt(0) == 3);
	CHECK(history.FrameAt(2) == 5);
}

TEST_CASE("A pass that appears part way through lands in execution order", "[passhistory]")
{
	bgl::PassHistory history;

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

TEST_CASE("A frame's total is what its passes cost together", "[passhistory]")
{
	bgl::PassHistory history;

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

TEST_CASE("Clearing forgets the frames and the passes", "[passhistory]")
{
	bgl::PassHistory history;

	history.Append(Frame(1, { { .name = "Clear", .milliseconds = 0.5 } }));
	history.Clear();

	CHECK(history.SampleCount() == 0);
	CHECK(history.Passes().empty());

	// The id guard goes with them: the same frame is a new recording after a clear, which is what a
	// viewport switching back to the one it was showing depends on.
	history.Append(Frame(1, { { .name = "Clear", .milliseconds = 0.5 } }));
	CHECK(history.SampleCount() == 1);
}

TEST_CASE("The CSV holds a header even with nothing recorded", "[passhistory]")
{
	CHECK(bgl::PassHistoryCsv(bgl::PassHistory()) == "sample,frame,total\n");
}

TEST_CASE("The CSV lists one row per sample, oldest first, with a total", "[passhistory]")
{
	bgl::PassHistory history;
	history.Append(Frame(
		11,
		{ { .name = "Clear", .milliseconds = 0.125 },
	      { .name = "Forward 0", .milliseconds = 4.5 } }));
	history.Append(Frame(
		12,
		{ { .name = "Clear", .milliseconds = 0.125 },
	      { .name = "Forward 0", .milliseconds = 1.0 } }));

	const std::vector<std::string> lines = Lines(bgl::PassHistoryCsv(history));

	REQUIRE(lines.size() == 4);
	CHECK(lines[0] == "sample,frame,Clear,Forward 0,total");
	CHECK(lines[1] == "0,11,0.125,4.500,4.625");
	CHECK(lines[2] == "1,12,0.125,1.000,1.125");
	CHECK(lines[3].empty());
}

TEST_CASE("A pass that did not run leaves the field empty, not zero", "[passhistory]")
{
	bgl::PassHistory history;
	history.Append(Frame(1, { { .name = "Clear", .milliseconds = 0.125 } }));
	history.Append(Frame(
		2,
		{ { .name = "Clear", .milliseconds = 0.125 },
	      // Zero is a pass that ran and could not be sampled, and must survive as a figure.
	      { .name = "SceneUpdate 0", .milliseconds = 0.0 } }));

	const std::vector<std::string> lines = Lines(bgl::PassHistoryCsv(history));

	REQUIRE(lines.size() >= 3);
	CHECK(lines[1] == "0,1,0.125,,0.125");
	CHECK(lines[2] == "1,2,0.125,0.000,0.125");
}

TEST_CASE("A pass name carrying a comma stays one field", "[passhistory]")
{
	bgl::PassHistory history;
	history.Append(Frame(1, { { .name = "Forward, part 2", .milliseconds = 1.0 } }));

	const std::vector<std::string> lines = Lines(bgl::PassHistoryCsv(history));

	CHECK(lines[0] == "sample,frame,\"Forward, part 2\",total");
}
