#pragma once

#include <cstdint>
namespace bgl::idl
{
	struct RawEntry
	{
		uint32_t byteOffset = 0;

		[[nodiscard]]
		bool
		Null() const noexcept
		{
			return byteOffset == 0;
		}
	};

	static_assert(sizeof(RawEntry) == sizeof(uint32_t), "RawEntry must mirror the Slang layout");
}
