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

	/**
	 * @throws std::runtime_error unless `source` is a `.glb` -- a `.gltf`'s sidecar `.bin` and
	 *         image files cannot be one copied file with one content stamp; the message says
	 *         "export as .glb".
	 */
	void
	requireSelfContainedSource(const std::filesystem::path& source);

	/**
	 * @throws std::runtime_error if two submeshes share a name. The name is the import document's
	 *         binding key, so a collision could only ever mis-bind silently; the fix is naming the
	 *         meshes in the DCC.
	 */
	void
	requireUniqueSubmeshNames(const BMesh& mesh);

	/** `<dataRoot>/meshes_src/<name>.glb` -- where an import copies its source. */
	[[nodiscard]] std::filesystem::path
	importedSourcePathFor(const std::filesystem::path& dataRoot, std::string_view name);

	/** The `.bimport` beside the copied source. */
	[[nodiscard]] std::filesystem::path
	importDocumentPathFor(const std::filesystem::path& dataRoot, std::string_view name);

	/**
	 * Copies the self-contained source into `meshes_src/` and writes its import document beside
	 * it: the sample rate, and -- when `mesh` is given -- the submesh-name -> material bindings
	 * the mesh carries at this moment, which is how an import records what `attachMaterial` just
	 * chose and how an adoption records what an existing file already held. Null `mesh` is a
	 * clips-only import: parameters, no bindings.
	 *
	 * @throws what requireSelfContainedSource throws, and std::runtime_error on a copy or write
	 *         failure.
	 */
	void
	writeImportedSource(
		const std::filesystem::path& source,
		const std::filesystem::path& dataRoot,
		std::string_view             name,
		float                        sampleRate,
		const BMesh*                 mesh);

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

		// The category directory `path` sits under, whole -- `<dataRoot>/textures_src`, not
		// `textures_src`. Compared as a path rather than a name because an import named after its
		// own category would otherwise look like the category itself and be left behind.
		std::filesystem::path categoryRoot;
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
