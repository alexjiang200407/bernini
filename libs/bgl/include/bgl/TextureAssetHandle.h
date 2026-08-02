#pragma once
#include <core/containers/slot_handle.h>

namespace bgl
{
	struct TextureAssetHandle
	{
		core::slot_handle textureSlot;

		// See TextureHandle::bindlessIndex -- carried across so a texture reached through an asset
		// handle resolves without asking the resource manager again.
		uint32_t bindlessIndex = core::slot_handle::invalid_index;
	};
}
