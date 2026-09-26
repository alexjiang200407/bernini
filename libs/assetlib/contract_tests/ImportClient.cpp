#include "ImportClient.h"
#include <assetlib/AssetStore.h>
#include <assetlib/ImportIdentity.h>
#include <assetlib/asset_refs.h>
#include <assetlib/import_document.h>
#include <string_view>

namespace assetlib::test
{
	ImportDocument
	newMeshDocument(std::string_view source)
	{
		auto document       = ImportDocument();
		document.source     = source;
		document.identity   = makeImportIdentity(source);
		document.outputs    = { importOutputKey(document.identity, AssetType::kMesh) };
		document.textureDir = importTextureDirectory(document.identity);
		return document;
	}

	LoadedImport
	loadMeshSource(const AssetStore& store, std::string_view source)
	{
		auto loaded     = LoadedImport();
		loaded.import   = store.ResolveImport(source, AssetType::kMesh);
		loaded.geometry = store.LoadRegenMesh(loaded.import.outputKey);
		return loaded;
	}
}
