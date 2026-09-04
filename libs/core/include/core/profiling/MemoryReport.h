#pragma once

#include <core/profiling/memory.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace core::profiling
{
	/**
	 * The tag table and the process footprint as they stand, for logging or for a readout.
	 *
	 * `untagged` is `footprint - tagged.live`, saturating at zero: it is the memory the OS charges
	 * that no tag claimed, so it grows as the tags miss things and is the number that says a new
	 * allocation needs one. Zero on a platform reporting no footprint, where there is nothing to
	 * take a residual against.
	 */
	struct MemorySnapshot
	{
		/** Every tag of every enum that has charged anything — see `memory_tag_totals`. */
		std::vector<MemoryTagTotals> tags;

		MemoryTotals tagged;
		uint64_t     footprint;
		uint64_t     footprintPeak;
		uint64_t     untagged;
	};

	[[nodiscard]] MemorySnapshot
	memory_snapshot() noexcept;

	/** The snapshot as the lines a person reads, newline-separated and without a trailing one. */
	[[nodiscard]] std::string
	format_memory_report(const MemorySnapshot& snapshot);

	/** @return false if the file could not be written; a report is never worth failing a run over. */
	bool
	write_memory_report(const MemorySnapshot& snapshot, const std::filesystem::path& path) noexcept;

	/**
	 * Logs the memory report when it dies, and writes JSON beside it when given a path.
	 *
	 * Construct one at the top of `main`. **It reports peaks, not what is live when it runs**: by
	 * the end of `main` the subsystems have been torn down and every live count is near zero, which
	 * is why a peak survives the release that follows it.
	 *
	 * A guard rather than a static object in `core`, because static destruction order would decide
	 * whether the counters still existed when the report ran.
	 */
	class MemoryReport
	{
	public:
		/** @param jsonPath Where to write the machine-readable report; empty writes none. */
		explicit MemoryReport(std::filesystem::path jsonPath = {}) noexcept;

		~MemoryReport() noexcept;

		MemoryReport(const MemoryReport&) = delete;
		MemoryReport&
		operator=(const MemoryReport&) = delete;
		MemoryReport(MemoryReport&&)   = delete;
		MemoryReport&
		operator=(MemoryReport&&) = delete;

	private:
		std::filesystem::path m_JsonPath;
	};
}
