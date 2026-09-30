#pragma once
#include <core/containers/slot_handle.h>

namespace bgl
{
	/**
	 * A rig uploaded with IScene::AddRig: a skeleton and the clips cooked against it, shared by
	 * every geom skinned to it.
	 *
	 * Not owned by any geom, and not reference-counted here -- see IScene::DeleteRig.
	 */
	struct RigHandle
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
