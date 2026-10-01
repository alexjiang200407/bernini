#pragma once
#include <core/containers/slot_handle.h>

namespace bgl
{
	/** An instance block reserved with ISceneView::CreateMeshInstanceBlock, valid in that view only. */
	struct MeshInstanceBlockHandle
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
