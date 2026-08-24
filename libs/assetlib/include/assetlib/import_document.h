#pragma once
#include <assetlib_structs/Animation.h>
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
	 * beside the source it describes (`meshes_src/kirk.glb` -> `meshes_src/kirk.bimport`), so two
	 * branches merge it like code.
	 *
	 * Two halves with different duties: the `parameters` object changes what the importer computes,
	 * so its serialized subtree is what the cache key hashes; `bindings` never key -- a rebind must
	 * not stale a mesh. Keys a reader does not know stay in the half they arrived in
	 * (`extraParametersJson` / `extraJson`) and are written back on serialize, so a newer branch's
	 * parameter still reaches the key through a reader that has never heard of it.
	 */
	struct ImportDocument
	{
		float                        sampleRate = c_DefaultSampleRate;
		std::vector<MaterialBinding> bindings;
		std::string                  extraParametersJson = "{}";
		std::string                  extraJson           = "{}";

		bool
		operator==(const ImportDocument&) const = default;
	};

	/** `meshes_src/kirk.glb` -> `meshes_src/kirk.bimport`. */
	[[nodiscard]] std::string
	importDocumentKeyFor(std::string_view sourceKey);

	/** `meshes_src/kirk.bimport` -> `meshes_src/kirk.glb` -- the only source kind is a `.glb`. */
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
