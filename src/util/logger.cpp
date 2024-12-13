#include "util/logger.h"
#include <filesystem>

void logger::Init()
{
	namespace fs = std::filesystem;

	fs::path p = "./logs/Bernini.log";
	fs::create_directory(p.parent_path());

	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(p.string(), true);

	auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

	log->set_level(spdlog::level::level_enum::trace);
	log->flush_on(spdlog::level::level_enum::trace);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%H:%M:%S:%e] %v");
	
	spdlog::info("Logger Initialized");
}
