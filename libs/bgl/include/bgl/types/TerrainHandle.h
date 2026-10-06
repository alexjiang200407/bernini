#pragma once
#include <core/containers/slot_handle.h>

namespace bgl
{
	/** A terrain created with IScene::CreateTerrain, alive until IScene::DeleteTerrain. */
	struct TerrainHandle
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
