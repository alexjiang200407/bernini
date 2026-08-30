#pragma once

namespace editor
{
	/**
	 * Routes Qt's logging into the default spdlog logger's sinks for the rest of the process.
	 *
	 * Call after `core::logging::init_file_logger`, whose file this then shares: the editor's own
	 * `qInfo`/`qWarning`, bgl's renderer lines and assetlib's messages land in one file, in one
	 * order, on one clock. Two files with two clocks cannot be read as one timeline, and a load
	 * crosses both.
	 *
	 * Writes through the sinks rather than through the logger, because that logger's level is the
	 * *renderer's* -- `init_file_logger` applies `GraphicsOptions::logLevel`, `kError` unless a
	 * config says otherwise -- and the editor's diagnostics are not renderer chatter for that knob
	 * to silence. Each message is flushed for the same reason it cannot rely on `flush_on`.
	 *
	 * Calling it again re-reads the sinks and replaces the route, so the order against
	 * `init_file_logger` is the only thing that matters.
	 */
	void
	InstallQtLogRouting();
}
