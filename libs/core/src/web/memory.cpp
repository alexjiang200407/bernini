#include <core/platform/memory.h>

namespace core
{
	// Emscripten's heap is the whole of what a page may observe, and it reports no footprint the
	// host charges. Reported as absent rather than as the heap size, so the memory report names the
	// tags it does know instead of showing a residual it invented.
	ProcessMemory
	process_memory() noexcept
	{
		return ProcessMemory{};
	}
}
