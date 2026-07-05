#pragma once
#include <bgl/GeomType.h>
#include <core/containers/slot_handle.h>

namespace bgl
{
	struct GeomHandle
	{
		GeomType          geomType = GeomType::kInvalid;
		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const noexcept
		{
			return geomType != GeomType::kInvalid;
		}
	};
}