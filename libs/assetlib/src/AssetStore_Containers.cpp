#include <assetlib/AssetStore.h>
#include <assetlib/bmesh.h>

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

}
