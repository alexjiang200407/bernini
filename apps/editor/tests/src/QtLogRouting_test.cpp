#include "util/qt_logging.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include <QtLogging>

#include <cstddef>
#include <memory>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	/**
	 * Stands in for the file `init_file_logger` opens: a default logger whose sink keeps its lines
	 * in memory, so a test can read back what a run would have written.
	 *
	 * Puts the real default logger and Qt's own handler back on the way out. Both are process-wide,
	 * and the routing under test replaces each of them.
	 */
	class CapturedLog
	{
	public:
		explicit CapturedLog(const spdlog::level::level_enum level = spdlog::level::info) :
			m_Previous(spdlog::default_logger())
		{
			auto logger = std::make_shared<spdlog::logger>("captured", m_Sink);
			logger->set_level(level);
			logger->set_pattern("[%l] %v");
			spdlog::set_default_logger(std::move(logger));
		}

		~CapturedLog()
		{
			qInstallMessageHandler(nullptr);
			spdlog::set_default_logger(m_Previous);
		}

		CapturedLog(const CapturedLog&) = delete;
		CapturedLog(CapturedLog&&)      = delete;
		CapturedLog&
		operator=(const CapturedLog&) = delete;
		CapturedLog&
		operator=(CapturedLog&&) = delete;

		[[nodiscard]] std::vector<std::string>
		Lines() const
		{
			return m_Sink->last_formatted();
		}

		[[nodiscard]] size_t
		CountContaining(const std::string_view needle) const
		{
			const std::vector<std::string> lines = Lines();
			return static_cast<size_t>(std::ranges::count_if(lines, [needle](const std::string& l) {
				return l.find(needle) != std::string::npos;
			}));
		}

	private:
		std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> m_Sink =
			std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(2048);
		std::shared_ptr<spdlog::logger> m_Previous;
	};
}

TEST_CASE("Qt and spdlog write to the same place", "[log]")
{
	// The whole point of the routing: bgl logs through spdlog and the editor logs through Qt, and a
	// load crosses both. Two files with two clocks cannot be read as one timeline.
	const auto captured = CapturedLog();
	editor::InstallQtLogRouting();

	qWarning("through-qt");
	spdlog::warn("through-spdlog");

	CHECK(captured.CountContaining("through-qt") == 1);
	CHECK(captured.CountContaining("through-spdlog") == 1);
}

TEST_CASE("A Qt message survives the renderer's log level", "[log]")
{
	// The hazard, and the reason the routing writes through the sinks rather than the logger:
	// init_file_logger applies GraphicsOptions::logLevel, which is kError unless a config says
	// otherwise. Routing through the logger would drop every editor diagnostic in a default build.
	const auto captured = CapturedLog(spdlog::level::err);
	editor::InstallQtLogRouting();

	qInfo("quiet-logger-info");
	spdlog::info("silenced-by-the-level");

	CHECK(captured.CountContaining("quiet-logger-info") == 1);

	// The renderer's knob still means what it means -- it is only the editor's own half that is
	// exempt.
	CHECK(captured.CountContaining("silenced-by-the-level") == 0);
}

TEST_CASE("A Qt message is readable before the call that logged it returns", "[log]")
{
	// flush_on belongs to the renderer's level, which this route deliberately does not share, so
	// the flush has to be its own. A crash log is written by the crash that would otherwise lose it.
	const auto captured = CapturedLog(spdlog::level::err);
	editor::InstallQtLogRouting();

	qCritical("flushed-now");

	CHECK(captured.CountContaining("flushed-now") == 1);
}

TEST_CASE("Concurrent writers never lose or tear a line", "[log]")
{
	// Qt calls the handler on whatever thread logged -- the render thread among them -- so the
	// route has to hold up under several at once. This pinned a real bug when the editor owned its
	// own file handle, and it is worth keeping now that a shared spdlog sink is what guarantees it.
	constexpr int c_Threads           = 4;
	constexpr int c_MessagesPerThread = 250;

	const auto captured = CapturedLog();
	editor::InstallQtLogRouting();

	auto writers = std::vector<std::thread>();
	for (int t = 0; t < c_Threads; ++t)
		writers.emplace_back([t] {
			for (int i = 0; i < c_MessagesPerThread; ++i) qWarning("%c-%d", 'a' + t, i);
		});

	for (std::thread& writer : writers) writer.join();

	const std::vector<std::string> lines = captured.Lines();
	REQUIRE(lines.size() == c_Threads * c_MessagesPerThread);

	// Every line whole: one level tag and one message, never two messages spliced together.
	for (const std::string& line : lines) CHECK(line.find("][") == std::string::npos);
}
