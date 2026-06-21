#pragma once
#include <core/containers/multi_slot_handle.h>

namespace bgl::db
{
	struct Range
	{
	public:
		uint32_t offsetStart;

		[[nodiscard]]
		bool
		Null() const noexcept
		{
			return offsetStart == 0xFFFFFFFF;
		}

		Range&
		operator=(core::multi_slot_handle handle) noexcept
		{
			offsetStart = handle.index;
			return *this;
		}
	};

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
