#pragma once

#include <cstdint>
namespace core
{
	/**
	 * What the operating system says this process owns, which is never what it allocated.
	 *
	 * `footprint` counts GPU, driver and compressed pages that no allocation tag can see, so the
	 * gap between it and the tagged total is what says whether the tags tell the whole story. It is
	 * also the number a low-memory killer reads: on Apple platforms this is `phys_footprint`, which
	 * is precisely what jetsam terminates on.
	 */
	struct ProcessMemory
	{
		/** Bytes the OS charges this process, or 0 where the platform exposes none. */
		uint64_t footprint;

		/** High-water `footprint`, or 0 where the platform does not record one. */
		uint64_t peak;
	};

	/** Zeroes rather than throwing when the platform query fails; a reading is never load-bearing. */
	[[nodiscard]] ProcessMemory
	process_memory() noexcept;
}
