#pragma once
#include <bgl/MaterialType.h>
#include <core/containers/slot_handle.h>

namespace bgl
{
	struct MaterialHandle
	{
		MaterialType      materialType = MaterialType::kInvalid;
		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const noexcept
		{
			return materialType != MaterialType::kInvalid;
		}
	};
}