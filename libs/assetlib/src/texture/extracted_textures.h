#pragma once

namespace assetlib
{
	class AssetStore;
	struct ImportDocument;

	/**
	 * Whether a file `document.textures` names is absent. False for a document that lists none --
	 * one from before the field says nothing about its folder's files.
	 */
	[[nodiscard]] bool
	missesExtractedTexture(const AssetStore& store, const ImportDocument& document);
}
