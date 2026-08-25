#include "mesh_load.h"
#include <assetlib/codecs.h>

#include <QDebug>

#include <assetlib/AssetCodec.h>
#include <assetlib/AssetStore.h>
#include <assetlib/RegenMesh.h>
#include <assetlib_structs/BMesh.h>
#include <core/file/file.h>

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

		// No project owns this file -- either the editor has no data root open, or the mesh sits
		// outside it. Bytes off the host, decoded by the codec: a store cannot answer for a path
		// it does not contain, and pretending otherwise is what the key/path split exists to stop.
		return assetlib::AssetCodec<assetlib::BMesh>::Deserialize(
			core::file::read_file_bytes(path.string()));
	}
}
