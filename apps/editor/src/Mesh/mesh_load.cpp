#include "mesh_load.h"

#include <QDebug>

#include <assetlib/AssetStore.h>
#include <assetlib/RegenMesh.h>
#include <assetlib/bmesh_io.h>
#include <assetlib_structs/BMesh.h>

namespace editor
{
	assetlib::BMesh
	LoadMeshThroughSeam(const std::filesystem::path& dataRoot, const std::filesystem::path& path)
	{
		if (!dataRoot.empty())
		{
			std::error_code             ec;
			const std::filesystem::path rel = std::filesystem::relative(path, dataRoot, ec);
			if (!ec && !rel.empty() && *rel.begin() != "..")
			{
				assetlib::RegenMesh current =
					assetlib::AssetStore(dataRoot).LoadRegenMesh(rel.generic_string());
				for (const std::string& submesh : current.unboundBindings)
					qWarning(
						"'%s': its import document binds submesh '%s', which the mesh no longer "
						"has; rebind or re-export",
						rel.generic_string().c_str(),
						submesh.c_str());
				return std::move(current.mesh);
			}
		}

		return assetlib::load(path);
	}
}
