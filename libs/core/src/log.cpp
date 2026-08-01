#include <core/log/log.h>

#include <core/file/file.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace core::logging
{
	void
	init_file_logger(const std::string_view fileName, const int level)
	{
		const std::filesystem::path path =
			core::file::get_executable_path().parent_path() / fileName;

		static bool g_Truncated = false;
		const bool  truncate    = !g_Truncated;
		g_Truncated             = true;

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), truncate);
		auto log  = std::make_shared<spdlog::logger>("global log", std::move(sink));

		log->set_level(static_cast<spdlog::level::level_enum>(level));
		log->flush_on(static_cast<spdlog::level::level_enum>(level));

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S:%e] [thread %t] [%l] %v");
	}
}
