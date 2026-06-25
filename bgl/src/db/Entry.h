#pragma once
#include <core/containers/slot_handle.h>

namespace bgl::db
{
	struct Entry
	{
		uint32_t offset = 0;

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
}
