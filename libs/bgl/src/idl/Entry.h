#pragma once
#include <core/containers/slot_handle.h>

namespace bgl::idl
{
	struct Entry
	{
		uint32_t offset = 0xFFFFFFFF;

		[[nodiscard]]
		bool
		Null() const noexcept
		{
			return offset == 0xFFFFFFFF;
		}

		Entry&
		operator=(core::slot_handle handle) noexcept
		{
			offset = handle.index;
			return *this;
		}
	};

	static_assert(sizeof(Entry) == sizeof(uint32_t), "Entry must mirror the Slang layout");
}
