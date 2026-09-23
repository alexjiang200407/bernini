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
	 * The looks to list under a submesh whose default is the material at `defaultPath`: every
	 * registered one except those naming that same file, which the default row already shows.
	 *
	 * A look does not stop being registered when it becomes the default -- a game that asks for it
	 * by name still gets it -- so this is what keeps it from appearing twice.
	 *
	 * @param dataRoot What a registered look's stored key is relative to.
	 */
	[[nodiscard]] std::vector<RegisteredMaterial>
	LooksBesidesDefault(
		const std::vector<RegisteredMaterial>& registered,
		const QString&                         defaultPath,
		const std::filesystem::path&           dataRoot);

	/**
	 * The name to register the outgoing default under when a look replaces it, or empty when there
	 * is nothing to keep: no file, or a registration that already names it.
	 *
	 * Without this the material a submesh used to load with leaves the list the moment another look
	 * takes over, and nothing in the project names it any more. Named from its file, stepped past a
	 * name already taken (`Crate`, then `Crate 2`).
	 */
	[[nodiscard]] QString
	NameForOutgoingDefault(
		const std::vector<RegisteredMaterial>& registered,
		const QString&                         defaultPath,
		const std::filesystem::path&           dataRoot);

	/**
	 * Where the copy backing a new look called `name` is written: `Rusty.bmaterial` beside `from`,
	 * or under the project's Materials directory when that material has no file yet.
	 *
	 * Named for the look alone. A project keeps a mesh's materials in a directory of its own, so
	 * the folder already says which Rusty this is and a stem in front of it would only repeat it.
	 * A spelling already on disk is stepped past (`Rusty_2.bmaterial`): two submeshes sharing a
	 * material and a name would otherwise compute one destination, and the second copy would
	 * overwrite the first's look.
	 */
	[[nodiscard]] QString
	NewOverrideMaterialPath(
		const std::filesystem::path& dataRoot,
		const QString&               from,
		const QString&               name);
}
