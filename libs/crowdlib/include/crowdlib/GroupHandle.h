#pragma once
#include <core/containers/slot_handle.h>

namespace crowd
{
	/** One group of an ICrowd, from the call that made it until the call that releases it. */
	struct GroupHandle
	{
		core::slot_handle handle;

		[[nodiscard]] bool
		IsValid() const noexcept
		{
			return !handle.is_null();
		}

		[[nodiscard]] bool
		operator==(const GroupHandle& other) const noexcept = default;
	};
}
