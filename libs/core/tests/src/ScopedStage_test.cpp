#include <core/log/ScopedStage.h>

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

using namespace core::logging;

namespace
{
	/**
	 * Swaps the default logger for one that keeps its lines in memory, and puts the real one back.
	 *
	 * The stage writes through the default logger, so a test that did not restore it would leave
	 * every later case in this binary logging into a dead ring buffer.
	 */
	class CapturedLog
	{
	public:
		explicit CapturedLog(const spdlog::level::level_enum level = spdlog::level::info) :
			m_Previous(spdlog::default_logger())
		{
			auto logger = std::make_shared<spdlog::logger>("captured", m_Sink);
			logger->set_level(level);
			logger->set_pattern("%v");
			spdlog::set_default_logger(std::move(logger));
		}

		~CapturedLog() { spdlog::set_default_logger(m_Previous); }

		CapturedLog(const CapturedLog&) = delete;
		CapturedLog&
		operator=(const CapturedLog&) = delete;

		[[nodiscard]] std::vector<std::string>
		Lines() const
		{
			return m_Sink->last_formatted();
		}

	private:
		std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> m_Sink =
			std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(16);
		std::shared_ptr<spdlog::logger> m_Previous;
	};

	/** Spins for at least `ms`. Sleeping would hand the scheduler a reason to overshoot wildly. */
	void
	BurnMilliseconds(const double ms)
	{
		const auto start = std::chrono::steady_clock::now();
		while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
		           .count() < ms)
		{}
	}
}

TEST_CASE("A stage says how long it took", "[log][scopedstage]")
{
	const auto captured = CapturedLog();

	{
		const auto stage = ScopedStage("bake posed bounds, 27 entries");
		BurnMilliseconds(2.0);
	}

	const std::vector<std::string> lines = captured.Lines();
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].find("bake posed bounds, 27 entries") != std::string::npos);
	CHECK(lines[0].find(" ms") != std::string::npos);
}

TEST_CASE("A stage formats its dimensions into the line", "[log][scopedstage]")
{
	// The dimensions are the whole point of naming a stage: a duration alone says a cook was slow
	// and never what its cost was a product of.
	const auto captured = CapturedLog();

	{
		const auto stage = ScopedStage("posed bounds: {} bones, {} frames", 663, 2254);
	}

	const std::vector<std::string> lines = captured.Lines();
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].find("posed bounds: 663 bones, 2254 frames") != std::string::npos);
}

TEST_CASE("A stage logs nothing until it ends", "[log][scopedstage]")
{
	const auto captured = CapturedLog();

	const auto stage = ScopedStage("still running");
	BurnMilliseconds(2.0);

	// The line is what a *finished* stage costs; one emitted on entry would report a duration of
	// zero and read as a stage that was free.
	CHECK(captured.Lines().empty());
}

TEST_CASE("A stage survives a logger that only wants errors", "[log][scopedstage]")
{
	// The default logger's level is the renderer's -- init_file_logger sets it from
	// GraphicsOptions::logLevel, which is kError. A stage filtered by it would be invisible in the
	// editor, which is the one place a slow cook most needs explaining.
	const auto captured = CapturedLog(spdlog::level::err);

	{
		const auto stage = ScopedStage("cook under a quiet logger");
	}

	const std::vector<std::string> lines = captured.Lines();
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].find("cook under a quiet logger") != std::string::npos);
}

TEST_CASE("A stage reports the time it has run so far", "[log][scopedstage]")
{
	const auto captured = CapturedLog();

	const auto stage = ScopedStage("split me");
	BurnMilliseconds(2.0);

	const double first = stage.Elapsed().count();
	CHECK(first >= 2.0);

	BurnMilliseconds(2.0);
	CHECK(stage.Elapsed().count() > first);
}
