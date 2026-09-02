#pragma once
#include <core/containers/slot_handle.h>

namespace bgl::idl
{
	struct Entry
	{
		uint32_t offset = 0;

		[[nodiscard]]
		bool
		Null() const noexcept
		{
			return offset == 0;
		}

		// A null handle carries slot_handle's own sentinel, which is not this one.
		Entry&
		operator=(core::slot_handle handle) noexcept
		{
			offset = handle.is_null() ? 0 : handle.index;
			return *this;
		}
	};

	static_assert(sizeof(Entry) == sizeof(uint32_t), "Entry must mirror the Slang layout");
}
