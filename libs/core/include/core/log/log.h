#pragma once

namespace core::logging
{
	/**
	 * Points the default logger at `<executable dir>/<fileName>` and fixes the message pattern.
	 *
	 * Truncates once per process, so a run accumulates every caller's messages instead of each new
	 * one clobbering what came before -- two devices in one process share the file rather than
	 * racing for it.
	 *
	 * @param level A spdlog level, as the renderer's log-level option carries it.
	 */
	void
	init_file_logger(std::string_view fileName, int level);
}
