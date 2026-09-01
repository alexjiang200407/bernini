#pragma once
#include <core/containers/slot_handle.h>

namespace bgl
{
	struct MeshInstanceHandle
	{
		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const noexcept
		{
			return !handle.is_null();
		}
	};
}