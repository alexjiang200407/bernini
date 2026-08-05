#pragma once
#include <bgl/LayerType.h>
#include <bgl/MaterialType.h>
#include <core/containers/slot_handle.h>

namespace bgl
{
	struct MaterialHandle
	{
		MaterialType materialType = MaterialType::kInvalid;

		// Orthogonal to the type: it is the (layer, type) pair that picks the PSO bucket, so a
		// submesh cannot know which bucket it belongs in from the material's storage alone.
		LayerType layerType = LayerType::kOpaque;

		// Blend only: draw a depth-only pre-pass so the surface self-occludes (hair, foliage) instead
		// of blending every layer through. Selects the kTransparentOcclude PSO bucket.
		bool occlude = false;

		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const noexcept
		{
			return materialType != MaterialType::kInvalid;
		}
	};
}