#pragma once
#include <core/containers/slot_handle.h>

namespace bgl
{
	/**
	 * A grass look created with IScene::CreateGrass, bound to a static geom's grass fields by
	 * AddStaticMeshGeom.
	 *
	 * Not owned by any geom, and not reference-counted here -- see IScene::DeleteGrass.
	 */
	struct GrassHandle
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
