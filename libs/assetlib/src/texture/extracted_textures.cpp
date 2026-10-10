#include "texture/extracted_textures.h"

#include <assetlib/AssetStore.h>
#include <assetlib/import_document.h>

#include <algorithm>
#include <string>

namespace assetlib
{
	bool
	missesExtractedTexture(const AssetStore& store, const ImportDocument& document)
	{
		return document.textures &&
		       std::ranges::any_of(*document.textures, [&store](const std::string& key) {
				   return !store.Exists(key);
			   });
	}
}
