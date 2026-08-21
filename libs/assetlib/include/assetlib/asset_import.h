#pragma once
#include <assetlib_structs/BMeshImport.h>

namespace assetlib
{
	struct BMesh;
	struct Skeleton;

	/**
	 * Writes `mesh` to `bmeshPath`, creating the directory the path names first: an import aimed
	 * at a subfolder lands at `Meshes/<folder>/`, which nothing else creates -- `save` opens the file
	 * where it stands.
	 *
	 * @throws std::runtime_error if the file cannot be written.
	 */
	void
	writeImportedMesh(const BMesh& mesh, const std::filesystem::path& bmeshPath);

	/**
	 * Writes the rig a skinned import carries -- the `.bskel` always, the `.banim` only when asked --
	 * and points `mesh` at the skeleton by a data-root-relative path.
	 *
	 * The skeleton is not optional and is deliberately not behind the import dialog's checkbox. A joint
	 * index is a bare number into a bone array, so a mesh carrying joints while naming no skeleton is
	 * one `save` refuses outright; the clips are the half a user can decline.
	 *
	 * Does nothing when the import carried no skin, which is what a static mesh is.
	 *
	 * @throws std::runtime_error if either file cannot be written.
	 */
	void
	writeImportedRig(
		const imp::BMeshImport&      imported,
		BMesh&                       mesh,
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& bskelPath,
		const std::filesystem::path& banimPath,
		bool                         writeClips);

	/**
	 * The `.bskel` under `dataRoot` whose signature matches `skeleton`, or empty when none does.
	 *
	 * What lets an import with the mesh turned off find the rig its clips belong to.
	 *
	 * @throws std::runtime_error if more than one rig matches, since which one the clips attach to
	 *         would otherwise depend on directory order.
	 */
	[[nodiscard]] std::filesystem::path
	findMatchingSkeleton(const std::filesystem::path& dataRoot, const Skeleton& skeleton);

	/**
	 * Writes only `imported`'s clips, attached to a rig already in the project -- what an import with
	 * the mesh turned off does, and how a rig whose animations the artist exported one per file gets
	 * all of them without a copy of the geometry each time.
	 *
	 * @throws std::runtime_error if the file carries no clips or no rig, or if no skeleton in
	 *         `dataRoot` matches the one it was authored against.
	 */
	void
	writeImportedClips(
		const imp::BMeshImport&      imported,
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& banimPath);

	/** A file an import writes, and whether the import is the one that made it. */
	struct ImportedFile
	{
		std::filesystem::path path;
		bool                  existed = false;  // whether it was there before the import started
	};

	/** A directory an import writes into, and whether the import is the one that made it. */
	struct ImportedDir
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
	rollBackImport(std::span<const ImportedFile> files, std::span<const ImportedDir> dirs);
}
