#pragma once

namespace assetlib
{
	struct BMesh;
}

namespace editor
{
	/**
	 * A mesh read through the regeneration seam: current against its copied source, its import
	 * document's bindings applied, and any binding naming a submesh the mesh no longer has
	 * reported to qWarning -- the strictest an editor load may be; `migrate` failing the file
	 * is where the report escalates.
	 *
	 * A mesh outside `dataRoot` -- or any mesh while no project is open (empty root) -- loads
	 * plainly: no project's documents describe it.
	 *
	 * @throws what AssetStore::LoadRegenMesh throws, and what assetlib::load throws on the plain
	 *         path.
	 */
	[[nodiscard]] assetlib::BMesh
	LoadMeshThroughSeam(const std::filesystem::path& dataRoot, const std::filesystem::path& path);
}
