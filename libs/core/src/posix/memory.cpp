#include <core/platform/memory.h>

#include <cstdint>
#include <mach/kern_return.h>
#include <mach/mach_init.h>
#include <mach/message.h>
#include <mach/task.h>
#include <mach/task_info.h>

namespace core
{
	ProcessMemory
	process_memory() noexcept
	{
		// resident_size is not what gets a process killed: jetsam reads phys_footprint, which also
		// carries IOKit (so GPU) allocations and compressed pages. TASK_VM_INFO is the only
		// task_info flavour reporting it, and the kernel answers with however many fields its
		// revision has -- phys_footprint arrived in rev1 and its ledger peak in rev3, so each is
		// only readable once `count` has come back covering it.
		task_vm_info_data_t    info{};
		mach_msg_type_number_t count = TASK_VM_INFO_COUNT;

		const kern_return_t result = ::task_info(
			::mach_task_self(),
			TASK_VM_INFO,
			reinterpret_cast<task_info_t>(&info),
			&count);
		if (result != KERN_SUCCESS || count < TASK_VM_INFO_REV1_COUNT)
			return ProcessMemory{};

		ProcessMemory memory{};
		memory.footprint = static_cast<uint64_t>(info.phys_footprint);
		if (count >= TASK_VM_INFO_REV3_COUNT && info.ledger_phys_footprint_peak > 0)
			memory.peak = static_cast<uint64_t>(info.ledger_phys_footprint_peak);

		return memory;
	}
}
