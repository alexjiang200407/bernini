#pragma once
#include <bgl/LayerType.h>
#include <bgl/MaterialType.h>
#include <cstdint>

namespace bgl
{
	struct MaterialHandle
	{
		MaterialType materialType = MaterialType::kInvalid;

		// Orthogonal to the type: it is the (layer, type) pair that decides how a submesh is drawn,
		// which a submesh cannot know from the material's storage alone.
		LayerType layerType = LayerType::kOpaque;

		// A byte offset into the scene's material arena, naming the record's header. Not generation
		// checked: a submesh stores the same offset, so a material outlived by its bindings is the
		// bargain IScene::DeleteMaterial documents.
		uint32_t byteOffset = 0;

		[[nodiscard]]
		bool
		IsValid() const noexcept
		{
			return materialType != MaterialType::kInvalid;
		}
	};
}