#pragma once
#include <core/containers/multi_slot_handle.h>

namespace bgl::idl
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
}
