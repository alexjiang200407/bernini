#include "error/GfxException.h"
#include "ffi/util.h"
#include <core/file/file.h>
#include <gfx/ffi/gfx.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace fs = std::filesystem;

static std::atomic_bool initialized = false;

GfxResult
initializeGfx(LogLevel berniniLogLevel)
{
	return gfx::ffi::apiInvoke(
		[=]() -> GfxResult {
			// Initialize spdlog global logger
			auto     libraryPath = core::file::getLibraryPath();
			fs::path logPath     = libraryPath.parent_path() / "gfx.log";

			auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);

			auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

			auto logLevel = static_cast<spdlog::level::level_enum>(berniniLogLevel);

			log->set_level(logLevel);
			log->flush_on(logLevel);

			spdlog::set_default_logger(std::move(log));
			spdlog::set_pattern("[%H:%M:%S:%e] [thread %t] [%l] %v"s);

			initialized.store(true, std::memory_order_release);

			gfx::logger::info("Gfx initialized successfully.");

			return GFX_RESULT_OK;
		},
		false);
}

bool
isGfxInitialized()
{
	return gfx::isGfxInitialized();
}

GfxErrorInfo
getLastError()
{
	return gfx::getLastErrorInfo();
}

namespace gfx
{
	bool
	isGfxInitialized() noexcept
	{
		return initialized.load(std::memory_order_acquire);
	}
}

const GfxVec3 GFX_UP_VECTOR = { .x = 0.0f, .y = 1.0f, .z = 0.0f };
