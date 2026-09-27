#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib/RegenMesh.h>
#include <editor_sdk/export.h>
#include <filesystem>

namespace editor
{
	/**
	 * Geometry and an owned binding snapshot read through the regeneration seam, with any
	 * binding naming a submesh the mesh no longer has
	 * reported to qWarning -- the strictest an editor load may be; `migrate` failing the file
	 * is where the report escalates.
	 *
	 * A mesh outside the store root -- loads
	 * plainly with empty bindings: no project's documents describe it.
	 *
	 * @throws what AssetStore::LoadRegenMesh throws, and what assetlib::load throws on the plain
	 *         path.
	 */
	[[nodiscard]] EDITOR_SDK_EXPORT assetlib::RegenMesh
	LoadMeshThroughSeam(const assetlib::AssetStore& store, const std::filesystem::path& path);
}
