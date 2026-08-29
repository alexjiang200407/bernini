#pragma once
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

namespace assetlib::test
{
	/**
	 * Swaps the default logger for one that keeps its lines in memory, and puts the real one back.
	 *
	 * A cook stage's cost is not visible in anything it returns -- a bake that reads a rig's
	 * vertices once and one that reads them per clip produce identical containers -- so the stage
	 * lines are what a test has to look at, the way CountingFileSystem looks at the reads.
	 *
	 * Defaults to `err`, which is what `init_file_logger` leaves the default logger at under the
	 * editor: a stage that is only captured at `info` is a stage the editor would never print.
	 */
	class CapturedLog
	{
	public:
		explicit CapturedLog(const spdlog::level::level_enum level = spdlog::level::err) :
			m_Previous(spdlog::default_logger())
		{
			auto logger = std::make_shared<spdlog::logger>("captured", m_Sink);
			logger->set_level(level);
			logger->set_pattern("%v");
			spdlog::set_default_logger(std::move(logger));
		}

		~CapturedLog() { spdlog::set_default_logger(m_Previous); }

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

		/** The one captured line containing `needle`, or nullopt when none or many do. */
		[[nodiscard]] std::optional<std::string>
		LineContaining(const std::string_view needle) const
		{
			auto found = std::optional<std::string>();
			for (const std::string& line : Lines())
			{
				if (line.find(needle) == std::string::npos)
					continue;
				if (found.has_value())
					return std::nullopt;
				found = line;
			}
			return found;
		}

	private:
		std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> m_Sink =
			std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(64);
		std::shared_ptr<spdlog::logger> m_Previous;
	};
}
