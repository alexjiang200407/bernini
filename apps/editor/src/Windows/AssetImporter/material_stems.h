#pragma once

#include <assetlib/bmesh_gltf.h>

#include <QStringList>
#include <qcontainerfwd.h>
#include <span>

namespace editor
{
	/**
	 * One `.bmaterial` file stem per material in a source's table, index-aligned with it, and empty
	 * where the material is not PBR and no file will be written.
	 *
	 * A glTF material name is free text: it can be empty, repeat, or carry separators that would send
	 * the file somewhere other than the import's own folder. Names that reduce to nothing are named by
	 * their index instead, and a repeat is suffixed -- case-insensitively, because "Rust" and "rust"
	 * are one file on Windows.
	 *
	 * The importer dialog seeds its name fields with these and the import writes whatever the fields
	 * then hold, so what a user is shown before an import is what lands after it.
	 */
	[[nodiscard]] QStringList
	MaterialStems(std::span<const assetlib::GltfMaterial> materials);
}
