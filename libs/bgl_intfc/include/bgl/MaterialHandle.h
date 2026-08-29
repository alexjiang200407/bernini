#pragma once
#include <bgl/LayerType.h>
#include <bgl/MaterialType.h>
#include <core/containers/slot_handle.h>

namespace bgl
{
	struct MaterialHandle
	{
		MaterialType materialType = MaterialType::kInvalid;

		// Orthogonal to the type: it is the (layer, type) pair that decides how a submesh is drawn,
		// which a submesh cannot know from the material's storage alone.
		LayerType layerType = LayerType::kOpaque;

		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const noexcept
		{
			return materialType != MaterialType::kInvalid;
		}
	};
}