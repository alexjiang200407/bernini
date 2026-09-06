#pragma once

#include <filesystem>
namespace editor
{
	/**
	 * The `config.json` deployed beside the executable — what the editor reads when nobody names
	 * one.
	 *
	 * Here rather than inside `MainWindow` because the window is no longer the only reader: the
	 * memory report is armed in `main` *before* the window exists, so that building the window is
	 * inside what it measures, and two spellings of where the file lives would be one to drift.
	 */
	[[nodiscard]] std::filesystem::path
	DefaultConfigPath();

	/**
	 * Whether this machine wants the memory report `main` writes at exit (`"memoryReport"`,
	 * defaulting to true).
	 *
	 * Read straight from disk rather than from the window's `core::Settings`, which does not exist
	 * yet at the point the answer is needed. A config that cannot be read answers true: the report
	 * is a diagnostic, and losing it silently is the worse failure.
	 */
	[[nodiscard]] bool
	MemoryReportEnabled(const std::filesystem::path& configPath);
}
