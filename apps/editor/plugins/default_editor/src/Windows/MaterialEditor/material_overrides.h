#pragma once

#include <QString>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace assetlib
{
	struct BMesh;
}

namespace editor
{
	/** One look a submesh registers: what it is called, and the material it names. */
	struct RegisteredMaterial
	{
		QString name;
		QString material;  // data-root-relative .bmaterial key

		bool
		operator==(const RegisteredMaterial&) const = default;
	};

	/**
	 * The looks `mesh` registers for its submesh at `sourceSubmesh`, by name, as the Material combo
	 * lists them under the submesh's default. Empty for a submesh that registers none.
	 *
	 * Read off the mesh rather than the `.bimport`, because that is where a load leaves them
	 * (`assetlib::rebuildMaterialSlots`) and the panel already holds one.
	 */
	[[nodiscard]] std::vector<RegisteredMaterial>
	RegisteredMaterialsFor(const assetlib::BMesh& mesh, uint32_t sourceSubmesh);

	/**
	 * Whether `name` can be registered for a submesh that already registers `taken`: a name is
	 * needed, and two looks of one submesh cannot share one -- the game asks for a look by name, so
	 * a duplicate is a request with two answers.
	 *
	 * Case-insensitively, for the reason IsSameMaterialFile compares that way.
	 */
	[[nodiscard]] bool
	CanRegisterMaterialName(const std::vector<RegisteredMaterial>& taken, const QString& name);

	/**
	 * Where the copy backing a new look called `name` is written: beside `from` when that material
	 * has a file, otherwise under the project's Materials directory, stemmed from `submeshName`.
	 *
	 * Named from what it varies rather than from the submesh alone -- `wood_Rusty.bmaterial` --
	 * because a project's materials are one flat directory and two submeshes called `crate[0]`
	 * in different meshes would otherwise collide. A spelling already on disk is stepped past
	 * (`wood_Rusty_2.bmaterial`): two submeshes sharing a material and a name would otherwise
	 * compute one destination, and the second copy would overwrite the first's look.
	 */
	[[nodiscard]] QString
	NewOverrideMaterialPath(
		const std::filesystem::path& dataRoot,
		const QString&               from,
		const QString&               submeshName,
		const QString&               name);
}
