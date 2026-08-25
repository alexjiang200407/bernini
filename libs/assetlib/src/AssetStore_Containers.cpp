#include <assetlib/AssetStore.h>
#include <assetlib/bmesh.h>
#include <assetlib/vat_bake.h>

#include <assetlib_structs/BVat.h>

#include "mounted_io.h"

namespace assetlib
{
	MeshRefs
	AssetStore::LoadMeshRefs(std::string_view path) const
	{
		return loadMeshRefs(*m_Files, path);
	}

	std::string
	AssetStore::LoadAnimationSkeletonPath(std::string_view path) const
	{
		return loadAnimationSkeletonPath(*m_Files, path);
	}

	BVat
	AssetStore::LoadVatTables(std::string_view path) const
	{
		return loadVatTables(*m_Files, path);
	}

	VatRefs
	AssetStore::LoadVatRefs(std::string_view path) const
	{
		return loadVatRefs(*m_Files, path);
	}
}
