#pragma once
#include "Range.h"

namespace bgl::idl
{
	struct RangeWithCount
	{
		Range    range;
		uint32_t count;

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
}
