#pragma once

#include <QString>

#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>

namespace assetlib
{
	struct Skeleton;
}

namespace editor::import
{
	/**
	 * Derives a `.bmaterial` from every PBR material `imported` carries, writes it under `materialDir`,
	 * and points each submesh cut from it at the file (relative to `dataRoot`, like every asset
	 * reference). Each is routed at the `texN.ktx2` files `writeTextures` puts in `textureDir`.
	 *
	 * Non-PBR materials are skipped, leaving their submeshes unassigned -- which both runtimes already
	 * render unlit. Every file is written before any submesh is pointed at one, so a failure part-way
	 * leaves a mesh naming only materials that exist.
	 *
	 * @param stems The file stem to write each material under, index-aligned with
	 *        `imported.materials` and empty where no file is wanted. The dialog shows these before the
	 *        import runs, so they are given rather than derived -- deriving them here as well is how a
	 *        preview and a file come to disagree. See editor::MaterialStems for the defaults.
	 * @throws std::runtime_error if a file cannot be written, or if `stems` does not cover
	 *         `imported.materials` -- which means the source was rewritten between the probe that
	 *         named them and the parse that produced these materials.
	 */
	void
	WriteMaterials(
		const assetlib::imp::BMeshImport& imported,
		assetlib::BMesh&                  mesh,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      materialDir,
		const std::filesystem::path&      textureDir,
		std::span<const QString>          stems);

	/**
	 * Writes the rig a skinned import carries -- the `.bskel` always, the `.banim` only when asked --
	 * and points `mesh` at the skeleton by a data-root-relative path.
	 *
	 * The skeleton is not optional and is deliberately not behind the dialog's checkbox. A joint index
	 * is a bare number into a bone array, so a mesh carrying joints while naming no skeleton is one
	 * `assetlib::save` refuses outright; the clips are the half a user can decline.
	 *
	 * Does nothing when the import carried no skin, which is what a static mesh is.
	 *
	 * @throws std::runtime_error if either file cannot be written.
	 */
	void
	WriteRig(
		const assetlib::imp::BMeshImport& imported,
		assetlib::BMesh&                  mesh,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      bskelPath,
		const std::filesystem::path&      banimPath,
		bool                              writeClips);

	/**
	 * Writes `mesh` to `bmeshPath`, creating the directory the path names first: an import aimed
	 * at a subfolder lands at `Meshes/<folder>/`, which nothing else creates -- `assetlib::save`
	 * opens the file where it stands.
	 *
	 * @throws std::runtime_error if the file cannot be written.
	 */
	void
	WriteMesh(const assetlib::BMesh& mesh, const std::filesystem::path& bmeshPath);

	/**
	 * The `.bskel` under `dataRoot` whose signature matches `skeleton`, or empty when none does.
	 *
	 * What lets an import with the mesh turned off find the rig its clips belong to.
	 *
	 * @throws std::runtime_error if more than one rig matches, since which one the clips attach to
	 *         would otherwise depend on directory order.
	 */
	[[nodiscard]] std::filesystem::path
	FindMatchingSkeleton(const std::filesystem::path& dataRoot, const assetlib::Skeleton& skeleton);

	/**
	 * Writes only `imported`'s clips, attached to a rig already in the project -- what an import with
	 * the mesh turned off does, and how a rig whose animations the artist exported one per file gets
	 * all of them without a copy of the geometry each time.
	 *
	 * @throws std::runtime_error if the file carries no clips or no rig, or if no skeleton in
	 *         `dataRoot` matches the one it was authored against.
	 */
	void
	WriteClips(
		const assetlib::imp::BMeshImport& imported,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      banimPath);

	/** A file an import writes, and whether the import is the one that made it. */
	struct WrittenFile
	{
		std::filesystem::path path;
		bool                  existed = false;  // whether it was there before the import started
	};

	/** A directory an import writes into, and whether the import is the one that made it. */
	struct WrittenDir
	{
		std::filesystem::path path;             // empty when the import writes no such directory
		bool                  existed = false;  // whether it was there before the import started
		std::string_view      categoryRoot;  // the category it sits under, never itself removable
	};

	/**
	 * Deletes what an import got as far as writing, so a cancelled or failed one leaves nothing behind:
	 * a `.bmesh` naming textures that were never extracted, or a half-supercompressed texture folder.
	 *
	 * Only what the import itself created, and never anything that was already there -- a texture folder
	 * that predates the import is left alone, files and all, because the user was asked before it was
	 * written into and its other contents are not ours to delete.
	 *
	 * A materials folder shared with another import is why the material files are listed in `files`
	 * one by one: the folder cannot be taken down, so the only way to leave the other import intact is
	 * to remove exactly the files this one wrote.
	 */
	void
	RollBack(std::span<const WrittenFile> files, std::span<const WrittenDir> dirs);
}
