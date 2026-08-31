#pragma once
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/SourceStamp.h>
#include <core/file/IFileSystem.h>

namespace assetlib
{
	/** One submesh's authored material choice, keyed by the name the submesh stores. */
	struct MaterialBinding
	{
		std::string
			submesh;  // Submesh::nameOffset's string, "<mesh>[p]" for a multi-primitive mesh
		std::string material;  // data-root-relative .bmaterial key

		bool
		operator==(const MaterialBinding&) const = default;
	};

	/**
	 * The authored half of one imported source: what a person chose at import and after it. Text,
	 * beside the source it describes (`Authored/Meshes/kirk.glb` ->
	 * `Authored/Meshes/kirk.bimport`), so two
	 * branches merge it like code.
	 *
	 * Two halves with different duties: the `parameters` object changes what the importer computes,
	 * so its serialized subtree is what the cache key hashes; `bindings`, `skeleton`, `outputs`,
	 * `textureDir` and `textureStamp` never key -- none of them changes what the importer
	 * computes. Keys a reader
	 * does not know stay in the half they arrived in
	 * (`extraParametersJson` / `extraJson`) and are written back on serialize, so a newer branch's
	 * parameter still reaches the key through a reader that has never heard of it.
	 */
	struct ImportDocument
	{
		float sampleRate = c_DefaultSampleRate;

		// The extracted textures' whole cache key, since a `.ktx2` carries none of its own: where
		// they went (empty when none), and the source as it stood when they were written.
		std::string textureDir;
		SourceStamp textureStamp;

		// Overrules what the cook measures for a named clip; see assetlib::groundClips. A parameter
		// rather than a binding: it changes the samples the importer writes, so it has to key.
		std::vector<ClipFloor> clipFloors;

		/** The `.bskel` this source's joint indices address; empty for a source with no rig. */
		std::string skeleton;

		/** Every container this source produced, as mount keys, sorted. See docs/asset_containers.md. */
		std::vector<std::string> outputs;

		std::vector<MaterialBinding> bindings;
		std::string                  extraParametersJson = "{}";
		std::string                  extraJson           = "{}";

		/**
		 * The `.bmesh` among `outputs`, or empty for a source that produced none -- a clips-only
		 * import, or one whose mesh has since been deleted out of the list.
		 *
		 * Here rather than in each caller because "which output is the mesh" is a fact about what
		 * an import writes, and two copies would drift the first time the answer stopped being
		 * "the one with a `.bmesh` extension".
		 */
		[[nodiscard]] std::string
		MeshOutput() const;

		bool
		operator==(const ImportDocument&) const = default;
	};

	/** `Authored/Meshes/kirk.glb` -> `Authored/Meshes/kirk.bimport`. */
	[[nodiscard]] std::string
	importDocumentKeyFor(std::string_view sourceKey);

	/**
	 * `Authored/Meshes/kirk.bimport` -> `Authored/Meshes/kirk.glb` -- the only source kind is a
	 * `.glb`.
	 */
	[[nodiscard]] std::string
	importedSourceKeyFor(std::string_view documentKey);

	/** @throws what `IFileSystem::Read` and `AssetCodec<ImportDocument>::Deserialize` throw. */
	[[nodiscard]] ImportDocument
	loadImportDocument(const core::file::IFileSystem& files, std::string_view key);

	/** The same read of a file on the host that no mount serves. */
	[[nodiscard]] ImportDocument
	loadImportDocument(const std::filesystem::path& path);

	/**
	 * The hash of the document's canonical parameter subtree -- the half of the cache key the
	 * document contributes. Unknown parameters hash too, so a newer branch's knob keys through a
	 * reader that has never heard of it; bindings never do.
	 */
	[[nodiscard]] uint64_t
	parametersHashOf(const ImportDocument& document);

}
