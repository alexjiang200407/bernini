#pragma once
#include <core/containers/multi_slot_handle.h>
#include <cstdint>

namespace bgl::idl
{
	struct Range
	{
	public:
		uint32_t offsetStart = 0;

		[[nodiscard]]
		bool
		Null() const noexcept
		{
			return offsetStart == 0;
		}

		// A null handle carries multi_slot_handle's own sentinel, which is not this one.
		Range&
		operator=(core::multi_slot_handle handle) noexcept
		{
			offsetStart = handle.is_null() ? 0 : handle.index;
			return *this;
		}
	};

	static_assert(sizeof(Range) == sizeof(uint32_t), "Range must mirror the Slang layout");
}
