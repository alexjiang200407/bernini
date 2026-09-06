#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>
namespace assetlib
{
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
	 * What an import is named and what it writes with, carried as one value so a write cannot take
	 * half of it. The data root is still the store's; `textureDir` is not one, and a person picks
	 * it in the importer.
	 */
	struct ImportTarget
	{
		std::string name;  // the copied source's stem: `Authored/Meshes/<name>.glb`
		float       sampleRate;
		std::string textureDir;  // where the textures went; empty when none were extracted

		// Filled in after the containers are written, since WriteImportedRig chooses the rig and an
		// output is not a key until it is one: the `.bskel` the joints address, and every container
		// this import wrote.
		std::string              skeleton{};
		std::vector<std::string> outputs{};
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

	/** One extracted texture that turned out to have moved rather than gone. */
	struct MovedTexture
	{
		std::string from;  // the key the materials were routed at
		std::string to;    // the key holding those same bytes now
	};

	/** What AssetStore::RefreshImportedTextures wrote, and what it left in the folder. */
	struct TextureRefresh
	{
		std::string              textureDir;
		std::vector<std::string> written;  // mount keys, sorted

		// Files the folder held that this extract did not produce and could not account for.
		// Reported, never removed or re-routed -- see docs/asset_containers.md.
		std::vector<std::string> superseded;

		/**
		 * The superseded files that were the same bytes as exactly one file this extract wrote --
		 * the same image under the name the current rule gives it. Those are moves, not losses, so
		 * the materials routing at them were rewritten and the old file is gone.
		 */
		std::vector<MovedTexture> moved;
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

		// The category directory `path` sits under, whole -- `<dataRoot>/Derived/SourceTextures`, not
		// `Derived/SourceTextures`. Compared as a path rather than a name because an import named
		// after its
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
