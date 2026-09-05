#pragma once
#include <assetlib/import_document.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/SourceRef.h>
#include <optional>
#include <string_view>

namespace assetlib
{
	class AssetStore;

	/** A source re-imported in memory, with the reference that keys everything derived from it. */
	struct RegeneratedGroup
	{
		imp::BMeshImport              import;
		SourceRef                     ref;
		std::optional<ImportDocument> document;
	};

	/**
	 * `sourceKey` parsed at `document`'s parameters, with textures skipped. Drives both directions:
	 * a stale container re-cooked in memory, and an absent one produced onto disk.
	 *
	 * @throws what `loadFromGltf` throws, and whatever reading the copied source throws.
	 */
	[[nodiscard]] RegeneratedGroup
	importGroup(const AssetStore& store, std::string_view sourceKey, ImportDocument&& document);
}
