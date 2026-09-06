#pragma once

#include <string_view>
namespace core::logging
{
	/**
	 * Points the default logger at `<executable dir>/<fileName>` and fixes the message pattern.
	 *
	 * **The first call wins the file; every call applies its level.** A process with two callers
	 * gets one log rather than the second one's -- the editor names the file before the renderer
	 * exists, and bgl's own call then only sets the level `GraphicsOptions::logLevel` asked for.
	 * Without that split a start-up timeline would straddle two files with two clocks.
	 *
	 * @param fileName Ignored after the first call.
	 * @param level A spdlog level, as the renderer's log-level option carries it.
	 * @throws spdlog::spdlog_ex if the file cannot be opened, on the first call only.
	 */
	void
	init_file_logger(std::string_view fileName, int level);
}
