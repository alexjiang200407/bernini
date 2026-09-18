#include <core/log/log.h>

#include <core/file/file.h>

#include <filesystem>
#include <memory>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <string_view>
#include <utility>

namespace
{
	constexpr auto c_LoggerName = "global log";
}

namespace core::logging
{
	void
	init_file_logger(const std::string_view fileName, const int level)
	{
		// Asked of spdlog's registry, which core_process holds once, rather than of a flag here: a
		// flag in core would be one per binary, and the renderer would open a log of its own.
		if (!spdlog::get(c_LoggerName))
		{
			const std::filesystem::path path =
				core::file::get_executable_path().parent_path() / fileName;

			auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
			spdlog::set_default_logger(
				std::make_shared<spdlog::logger>(c_LoggerName, std::move(sink)));
			spdlog::set_pattern("[%H:%M:%S:%e] [thread %t] [%l] %v");
		}

		const std::shared_ptr<spdlog::logger> log = spdlog::default_logger();
		log->set_level(static_cast<spdlog::level::level_enum>(level));
		log->flush_on(static_cast<spdlog::level::level_enum>(level));
	}
}
