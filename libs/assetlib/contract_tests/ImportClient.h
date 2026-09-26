#pragma once
#include <assetlib/RegenMesh.h>
#include <assetlib/ResolvedImport.h>
#include <assetlib/import_document.h>
#include <string_view>

namespace assetlib
{
	class AssetStore;
}

namespace assetlib::test
{
	struct LoadedImport
	{
		ResolvedImport import;
		RegenMesh      geometry;
	};

	ImportDocument
	newMeshDocument(std::string_view source);

	LoadedImport
	loadMeshSource(const AssetStore& store, std::string_view source);
}
