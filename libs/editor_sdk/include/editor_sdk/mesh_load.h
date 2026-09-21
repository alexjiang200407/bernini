#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib_structs/BMesh.h>
#include <editor_sdk/export.h>
#include <filesystem>

namespace editor
{
	/**
	 * A mesh read through the regeneration seam: current against its copied source, its import
	 * document's bindings applied, and any binding naming a submesh the mesh no longer has
	 * reported to qWarning -- the strictest an editor load may be; `migrate` failing the file
	 * is where the report escalates.
	 *
	 * A mesh outside the store root -- loads
	 * plainly: no project's documents describe it.
	 *
	 * @throws what AssetStore::LoadRegenMesh throws, and what assetlib::load throws on the plain
	 *         path.
	 */
	[[nodiscard]] EDITOR_SDK_EXPORT assetlib::BMesh
	LoadMeshThroughSeam(const assetlib::AssetStore& store, const std::filesystem::path& path);
}
