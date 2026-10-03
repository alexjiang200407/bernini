#pragma once
#include <core/containers/slot_handle.h>

namespace bgl
{
	/**
	 * A toon shading rig added with IScene::AddToonShadingRig, given to placements by
	 * ISceneView::SetToonShadingRig.
	 *
	 * Not owned by any placement, and not reference-counted here -- see IScene::DeleteToonShadingRig.
	 */
	struct ToonShadingRigHandle
	{
		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const noexcept
		{
			return !handle.is_null();
		}

		[[nodiscard]]
		bool
		operator==(const ToonShadingRigHandle&) const noexcept = default;
	};
}
