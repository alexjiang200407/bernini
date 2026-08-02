#pragma once
#include <core/containers/slot_handle.h>

namespace bgl
{
	struct TextureAssetHandle
	{
		core::slot_handle textureSlot;

		// What a shader must find in a constant buffer to sample this texture: the bindless index of
		// the SRV the scene created for it. A texture has no descriptor of its own, so this comes
		// from the view, and is carried here so a caller never has to ask the scene again.
		uint32_t srvBindlessIndex = core::slot_handle::invalid_index;
	};
}
