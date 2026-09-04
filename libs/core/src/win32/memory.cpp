#include <core/platform/memory.h>

#include "win32/WinAPI.h"

#include <psapi.h>

namespace core
{
	ProcessMemory
	process_memory() noexcept
	{
		// PrivateUsage, not WorkingSetSize: the working set is what is resident right now, so it
		// falls when the OS trims under pressure and reads as a process that shrank. Private commit
		// is what the process is actually charged for, and is the closest Win32 has to Apple's
		// phys_footprint.
		//
		// The peak must therefore be PeakPagefileUsage and not PeakWorkingSetSize: PrivateUsage is
		// documented as the same quantity as PagefileUsage, so that is its own high-water, while
		// the working set's peak is the high-water of the metric rejected just above -- and would
		// read *below* footprint for a process holding a large committed allocation the OS has
		// paged out, which is an asset cook's normal state.
		PROCESS_MEMORY_COUNTERS_EX counters{};
		counters.cb = sizeof(counters);

		if (!::GetProcessMemoryInfo(
				::GetCurrentProcess(),
				reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
				sizeof(counters)))
			return ProcessMemory{};

		return ProcessMemory{
			.footprint = static_cast<uint64_t>(counters.PrivateUsage),
			.peak      = static_cast<uint64_t>(counters.PeakPagefileUsage),
		};
	}
}
