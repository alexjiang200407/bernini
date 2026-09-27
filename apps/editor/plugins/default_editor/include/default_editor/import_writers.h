#pragma once

#include <QString>

#include <assetlib/import_document.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <filesystem>
#include <span>
#include <vector>

namespace editor
{
	/**
	 * Derives a `.bmaterial` from every PBR material `imported` carries, writes it under `materialDir`,
	 * and returns named sidecar bindings for its submeshes. Each material is routed at the `.ktx2`
	 * files `AssetStore::WriteTextures` puts in `textureDir`.
	 *
	 * Non-PBR materials are skipped, leaving their submeshes unassigned -- which both runtimes already
	 * render unlit. Every file is written before the bindings are returned.
	 *
	 * @param stems The file stem to write each material under, index-aligned with
	 *        `imported.materials` and empty where no file is wanted. The dialog shows these before the
	 *        import runs, so they are given rather than derived -- deriving them here as well is how a
	 *        preview and a file come to disagree. See editor::MaterialStems for the defaults.
	 * @throws std::runtime_error if a file cannot be written, or if `stems` does not cover
	 *         `imported.materials` -- which means the source was rewritten between the probe that
	 *         named them and the parse that produced these materials.
	 */
	[[nodiscard]] std::vector<assetlib::MaterialBinding>
	WriteImportedMaterials(
		const assetlib::imp::BMeshImport& imported,
		const assetlib::BMesh&            mesh,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      materialDir,
		const std::filesystem::path&      textureDir,
		std::span<const QString>          stems);
}
