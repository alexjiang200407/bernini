#pragma once

namespace core
{
	struct multi_slot_handle
	{
		static constexpr uint32_t invalid_index = 0xFFFFFFFFu;
		uint32_t                  index         = invalid_index;
		uint32_t                  count         = 0;
		uint32_t                  generation    = 0;

		[[nodiscard]] bool
		is_null() const
		{
			return index == invalid_index;
		}
	};
}