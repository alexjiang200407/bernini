#pragma once
#include "Range.h"

namespace bgl::idl
{
	struct RangeWithCount
	{
		Range    range;
		uint32_t count = 0;

		[[nodiscard]]
		bool
		Null() const noexcept
		{
			return range.Null();
		}

		RangeWithCount&
		operator=(core::multi_slot_handle handle) noexcept
		{
			range = handle;
			count = handle.count;
			return *this;
		}
	};

	static_assert(
		sizeof(RangeWithCount) == 2 * sizeof(uint32_t),
		"RangeWithCount must mirror the Slang layout");
}
