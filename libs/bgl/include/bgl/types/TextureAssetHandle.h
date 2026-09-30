#pragma once
#include <core/containers/slot_handle.h>
#include <cstdint>

namespace bgl
{
	struct TextureAssetHandle
	{
		core::slot_handle textureSlot;

		// What a shader must find in a constant buffer to reach this texture. How a renderer makes
		// one is its own business; the value belongs to the view the scene created, not to the
		// texture, and is carried here so a caller never has to ask the scene again.
		uint32_t shaderIndex = core::slot_handle::invalid_index;
	};
}
