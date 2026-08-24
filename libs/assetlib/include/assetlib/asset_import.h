#pragma once
#include <assetlib_structs/BMeshImport.h>

namespace assetlib
{
	class AssetStore;

	struct BMesh;
	struct MaterialBinding;
	struct Skeleton;
	struct SourceRef;

	/**
	 * Writes `mesh` at `bmeshKey`, creating the directory the key names first: an import aimed
	 * at a subfolder lands at `Meshes/<folder>/`, which nothing else creates -- a store's Save
	 * opens the file where it stands.
	 *
	 * @throws std::runtime_error if the file cannot be written. The store's *data root* must
	 *         already exist, which is AssetStore's own precondition rather than this one's: a
	 *         project always has one, and an import into a directory that is not there is a
	 *         mistyped root rather than a new subfolder.
	 */
	void
	writeImportedMesh(const AssetStore& store, const BMesh& mesh, std::string_view bmeshKey);

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
		const AssetStore&       store,
		const imp::BMeshImport& imported,
		BMesh&                  mesh,
		std::string_view        bskelKey,
		std::string_view        banimKey,
		bool                    writeClips,
		const SourceRef&        source);

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
		const AssetStore&       store,
		const imp::BMeshImport& imported,
		std::string_view        banimKey,
		const SourceRef&        source);

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

	/**
	 * Where an import writes and the parameters it writes with -- the trio every import write
	 * needs, carried as one value so a write cannot take half of it.
	 */
	struct ImportTarget
	{
		std::filesystem::path dataRoot;
		std::string           name;  // the copied source's stem: `meshes_src/<name>.glb`
		float                 sampleRate;
	};

	/** `<dataRoot>/meshes_src/<name>.glb` -- where an import copies its source. */
	[[nodiscard]] std::filesystem::path
	importedSourcePathFor(const std::filesystem::path& dataRoot, std::string_view name);

	/** The `.bimport` beside the copied source. */
	[[nodiscard]] std::filesystem::path
	importDocumentPathFor(const std::filesystem::path& dataRoot, std::string_view name);

	/**
	 * Copies the self-contained source into `meshes_src/` and stamps it: the returned reference --
	 * key, content stamp, parameter hash -- is what the caller sets on every container derived
	 * from it *before* saving them. The document itself is written afterwards by
	 * writeImportedDocument, once the bindings exist; the split is safe because bindings are
	 * deliberately outside the parameter hash.
	 *
	 * `target.sampleRate` -- the rate clips are resampled to at import, the import's one
	 * parameter -- is what the returned reference's parameter hash covers.
	 *
	 * @throws what requireSelfContainedSource throws, and std::runtime_error on a copy failure.
	 */
	SourceRef
	copyImportedSource(const std::filesystem::path& source, const ImportTarget& target);

	/**
	 * Writes the `.bimport` beside the copied source: the sample rate, and -- when `mesh` is
	 * given -- the submesh-name -> material bindings the mesh carries at this moment, which is how
	 * an import records what `attachMaterial` just chose and how an adoption records what an
	 * existing file already held. Null `mesh` is a clips-only import: parameters, no bindings.
	 *
	 * @throws std::runtime_error on a write failure.
	 */
	void
	writeImportedDocument(const ImportTarget& target, const BMesh* mesh);

	/**
	 * Rebuilds `mesh.materials` and every `Submesh::material` canonically from `bindings` -- a
	 * pure function of the document, never a mutation of what was loaded, so two checkouts with
	 * one document hold one array. A submesh the document does not name is unbound.
	 *
	 * @return The submeshes named by bindings this mesh does not have -- the source changed shape
	 *         under the document. Never guessed at: the editor warns, `migrate` fails the file,
	 *         `pack` fails the pack.
	 */
	[[nodiscard]] std::vector<std::string>
	applyBindings(BMesh& mesh, std::span<const MaterialBinding> bindings);

	/**
	 * Sets `submesh`'s binding to `material` in the import document beside `sourceKey`'s copied
	 * source, leaving everything else -- the parameters, unknown keys, every other binding --
	 * exactly as it stands. What a rebind in the editor writes instead of the mesh file: the
	 * binding is outside the cache key, so the mesh is neither rewritten nor staled.
	 *
	 * `sourceKey` is the mount key the mesh's header carries (`BMesh::source.key`); the document
	 * path is derived here, so no caller composes it.
	 *
	 * @throws std::runtime_error if `sourceKey` is empty, the document is absent or malformed, or
	 *         the write fails.
	 */
	void
	rebindSubmeshInDocument(
		const std::filesystem::path& dataRoot,
		std::string_view             sourceKey,
		std::string_view             submesh,
		std::string_view             material);

	/** What happened to one import document under reauthorImportDocuments. */
	struct ReauthoredDocument
	{
		enum class Outcome
		{
			kUnchanged,  // the bindings already matched the mesh
			kRewritten,
			kFailed  // `message` says why
		};

		std::string key;  // the document's mount key
		Outcome     outcome;
		std::string message;
	};

	/**
	 * Rewrites every import document's bindings under `dataRoot` from its mesh's current state,
	 * parameters and unknown keys preserved -- the one-time adoption pass that makes the documents
	 * authoritative. Until it runs, a rebind saved into a `.bmesh` before documents existed is
	 * recorded nowhere else; after it, the document is what a load applies, so running this again
	 * later would overwrite document-only rebinds with stale mesh state.
	 *
	 * A mesh that will not load, a source claimed by two meshes, or a recorded source whose
	 * document is missing is reported per document and never guessed at.
	 *
	 * @throws std::runtime_error if `dataRoot` is not a directory.
	 */
	[[nodiscard]] std::vector<ReauthoredDocument>
	reauthorImportDocuments(const std::filesystem::path& dataRoot);

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
