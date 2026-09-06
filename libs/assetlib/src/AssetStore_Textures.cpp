#include <assetlib/AssetStore.h>

#include <assetlib/image_io.h>
#include <assetlib_structs/ImageData.h>
#include <cstdint>
#include <string_view>

#include "mounted_io.h"

namespace assetlib
{
	ImageData
	AssetStore::LoadTexture(std::string_view path, Ktx2Decode decode, uint32_t maxDim) const
	{
		return loadKTX2(*m_Files, path, decode, maxDim);
	}

	ImageData
	AssetStore::LoadTexture(std::string_view path) const
	{
		return loadKTX2(*m_Files, path);
	}

	ImageData
	AssetStore::LoadTexturePreview(std::string_view path, uint32_t maxDim) const
	{
		return loadKTX2Preview(*m_Files, path, maxDim);
	}
}
