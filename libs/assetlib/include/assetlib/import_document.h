#pragma once
#include <assetlib/env_import_parameters.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/SourceStamp.h>
#include <core/file/IFileSystem.h>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
	 * so its serialized subtree is what the cache key hashes; `source`, `bindings`, `skeleton`,
	 * `outputs`, `textureDir`, the two stamp-and-token pairs and the per-part hashes an environment
	 * was written with never key -- none of them changes what the importer computes. Keys a reader
	 * does not know stay in the half they arrived in
	 * (`extraParametersJson` / `extraJson`) and are written back on serialize, so a newer branch's
	 * parameter still reaches the key through a reader that has never heard of it.
	 */
	struct ImportDocument
	{
		/**
		 * The copied source this document describes, as a mount key. Recorded rather than derived
		 * from the document's own name: a source kind may have more than one extension, and the
		 * swap that reaches `kirk.glb` from `kirk.bimport` has no answer for one that does.
		 *
		 * Empty in a document written before the field, where the swap was the only answer there
		 * was; `importedSourceKeyFor` is what knows that, and `AssetStore::Migrate` backfills it as
		 * it backfills `outputs`. Outside `parameters`, with `outputs`: naming the source does not
		 * change what the importer computes from it.
		 */
		std::string source;

		float sampleRate = c_DefaultSampleRate;

		// The extracted textures' whole cache key, since a `.ktx2` carries none of its own: where
		// they went (empty when none), the source as it stood when they were written, and
		// c_TextureBakeToken as it stood then -- zero in a document from before it existed.
		std::string textureDir;
		SourceStamp textureStamp;
		uint64_t    textureBakeToken = 0;

		// Overrules what the cook measures for a named clip; see assetlib::groundClips. A parameter
		// rather than a binding: it changes the samples the importer writes, so it has to key.
		std::vector<ClipFloor> clipFloors;

		/**
		 * Set for an environment source, and then the whole of its parameters: such a document
		 * writes no `sampleRate`, which nothing reads for it and which would otherwise key every
		 * environment on a mesh default.
		 */
		std::optional<EnvironmentImportParameters> environment;

		// The environment's key, as textureStamp and textureBakeToken are the extracted textures':
		// the source as it stood when its parts were cooked, and c_EnvSourceBakeToken then.
		SourceStamp envSourceStamp;
		uint64_t    envSourceBakeToken = 0;

		// And the parameters each part was written with, hashed per part. A `.bmesh` keeps this in
		// its own header; a baked map has none, and `environment` is what the parameters are *now*,
		// so without these an edited document would describe pixels nothing re-cooks. Zero for a
		// part this source never produced.
		uint64_t envSkyParametersHash      = 0;
		uint64_t envLightingParametersHash = 0;

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
		GetMeshOutput() const;

		bool
		operator==(const ImportDocument&) const = default;
	};

	/** `Authored/Meshes/kirk.glb` -> `Authored/Meshes/kirk.bimport`. */
	[[nodiscard]] std::string
	importDocumentKeyFor(std::string_view sourceKey);

	/**
	 * The source `document` describes -- its `source`, or, for a document written before that field
	 * existed, `Authored/Meshes/kirk.bimport` -> `Authored/Meshes/kirk.glb`, which was the only
	 * answer there was while a `.glb` was the only source kind.
	 *
	 * @param documentKey Read only to answer the second case.
	 */
	[[nodiscard]] std::string
	importedSourceKeyFor(std::string_view documentKey, const ImportDocument& document);

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
