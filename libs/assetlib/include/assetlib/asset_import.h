#pragma once

namespace assetlib
{
	class AssetStore;

	struct BMesh;
	struct MaterialBinding;
	struct Skeleton;
	struct SourceRef;

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
	 * What an import is named and the parameter it writes with, carried as one value so a write
	 * cannot take half of it. Where it lands is the store's, not this.
	 */
	struct ImportTarget
	{
		std::string name;  // the copied source's stem: `meshes_src/<name>.glb`
		float       sampleRate;
	};

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

	/** What happened to one import document under `AssetStore::ReauthorImportDocuments`. */
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
