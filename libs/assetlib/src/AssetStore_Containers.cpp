#include <assetlib/AssetStore.h>
#include <assetlib/bmesh.h>
#include <string>
#include <string_view>

#include "mounted_io.h"

namespace assetlib
{
	std::string
	AssetStore::LoadAnimationSkeletonPath(std::string_view path) const
	{
		return loadAnimationSkeletonPath(*m_Files, path);
	}

}
