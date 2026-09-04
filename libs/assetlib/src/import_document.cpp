#include <algorithm>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>

#include <core/err/util.h>
#include <core/file/file.h>
#include <core/hash.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "json_doc.h"
#include "ref_paths.h"
#include <assetlib_structs/Animation.h>

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_ParametersKey       = "parameters";
		constexpr std::string_view c_SampleRateKey       = "sampleRate";
		constexpr std::string_view c_ClipFloorKey        = "clipFloor";
		constexpr std::string_view c_BindingsKey         = "bindings";
		constexpr std::string_view c_TextureDirKey       = "textureDir";
		constexpr std::string_view c_TextureStampSizeKey = "textureStampSize";
		constexpr std::string_view c_TextureStampHashKey = "textureStampHash";
		constexpr std::string_view c_SkeletonKey         = "skeleton";
		constexpr std::string_view c_OutputsKey          = "outputs";

		/**
		 * The document's parameter subtree, built once: this is both what Serialize writes and what
		 * parametersHashOf hashes, so a parameter cannot reach the file without reaching the key.
		 *
		 * An empty `clipFloor` is omitted rather than written, so a document that authors none
		 * hashes exactly as it did before the key existed -- writing `{}` would stale every
		 * container in every project.
		 */
		nlohmann::json
		parametersObject(const ImportDocument& document)
		{
			auto parameters = doc::parseObject(
				document.extraParametersJson,
				"import document: extraParametersJson");
			parameters[c_SampleRateKey] = doc::plainFloat(document.sampleRate);

			if (!document.clipFloors.empty())
			{
				auto grounds = nlohmann::json::object();
				for (const ClipFloor& ground : document.clipFloors)
				{
					core::throw_runtime_error_if(
						grounds.contains(ground.clip),
						"import document: two authored grounds for clip '{}'",
						ground.clip);
					grounds[ground.clip] = doc::plainFloat(ground.floor);
				}
				parameters[c_ClipFloorKey] = std::move(grounds);
			}

			return parameters;
		}

		std::string
		swapExtension(std::string_view key, std::string_view extension)
		{
			const std::string ext = extensionOf(key);
			core::throw_runtime_error_if(
				ext.empty(),
				"import document: '{}' has no extension",
				key);
			return std::string(key.substr(0, key.size() - ext.size())).append(extension);
		}

	}

	std::string
	ImportDocument::GetMeshOutput() const
	{
		for (const std::string& output : outputs)
			if (assetTypeFromExtension(output) == AssetType::kMesh)
				return output;

		return {};
	}

	std::string
	importDocumentKeyFor(std::string_view sourceKey)
	{
		return swapExtension(sourceKey, c_ImportDocumentExtension);
	}

	std::string
	importedSourceKeyFor(std::string_view documentKey)
	{
		return swapExtension(documentKey, c_ImportedSourceExtension);
	}

	ImportDocument
	AssetCodec<ImportDocument>::Deserialize(std::span<const std::byte> bytes)
	{
		const auto text =
			std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());

		auto json = doc::parseObject(text, "import document: the document");

		ImportDocument document;

		if (auto it = json.find(c_ParametersKey); it != json.end())
		{
			core::throw_runtime_error_if(
				!it->is_object(),
				"import document: '{}' is not an object",
				c_ParametersKey);
			if (auto rate = it->find(c_SampleRateKey); rate != it->end())
			{
				core::throw_runtime_error_if(
					!rate->is_number() || rate->get<float>() <= 0.0f,
					"import document: '{}' is not a positive number",
					c_SampleRateKey);
				document.sampleRate = rate->get<float>();
				it->erase(rate);
			}
			if (auto grounds = it->find(c_ClipFloorKey); grounds != it->end())
			{
				core::throw_runtime_error_if(
					!grounds->is_object(),
					"import document: '{}' is not an object",
					c_ClipFloorKey);
				for (const auto& [clip, floor] : grounds->items())
				{
					core::throw_runtime_error_if(
						!floor.is_number(),
						"import document: the authored ground for clip '{}' is not a number",
						clip);
					document.clipFloors.push_back({ clip, floor.get<float>() });
				}
				it->erase(grounds);
			}
			document.extraParametersJson = it->dump();
			json.erase(it);
		}

		if (auto it = json.find(c_TextureDirKey); it != json.end())
		{
			core::throw_runtime_error_if(
				!it->is_string(),
				"import document: '{}' is not a string",
				c_TextureDirKey);
			document.textureDir = it->get<std::string>();
			json.erase(it);
		}

		for (const auto& [stampKey, field] :
		     { std::pair<std::string_view, uint64_t*>{ c_TextureStampSizeKey,
		                                               &document.textureStamp.size },
		       { c_TextureStampHashKey, &document.textureStamp.hash } })
		{
			if (const auto it = json.find(stampKey); it != json.end())
			{
				core::throw_runtime_error_if(
					!it->is_number_unsigned(),
					"import document: '{}' is not an unsigned number",
					stampKey);
				*field = it->get<uint64_t>();
				json.erase(it);
			}
		}

		if (auto it = json.find(c_SkeletonKey); it != json.end())
		{
			core::throw_runtime_error_if(
				!it->is_string(),
				"import document: '{}' is not a string",
				c_SkeletonKey);
			document.skeleton = it->get<std::string>();
			json.erase(it);
		}

		if (auto it = json.find(c_OutputsKey); it != json.end())
		{
			core::throw_runtime_error_if(
				!it->is_array(),
				"import document: '{}' is not an array",
				c_OutputsKey);
			for (const auto& output : *it)
			{
				core::throw_runtime_error_if(
					!output.is_string(),
					"import document: '{}' holds a non-string entry",
					c_OutputsKey);
				document.outputs.push_back(output.get<std::string>());
			}
			json.erase(it);
		}

		if (auto it = json.find(c_BindingsKey); it != json.end())
		{
			core::throw_runtime_error_if(
				!it->is_object(),
				"import document: '{}' is not an object",
				c_BindingsKey);
			for (const auto& [submesh, material] : it->items())
			{
				core::throw_runtime_error_if(
					!material.is_string(),
					"import document: binding '{}' is not a string",
					submesh);
				document.bindings.push_back({ submesh, material.get<std::string>() });
			}
			json.erase(it);
		}

		document.extraJson = json.dump();
		return document;
	}

	std::vector<std::byte>
	AssetCodec<ImportDocument>::Serialize(const ImportDocument& document)
	{
		auto json = doc::parseObject(document.extraJson, "import document: extraJson");

		json[c_ParametersKey] = parametersObject(document);

		// Omitted rather than written empty, so a document for an import that extracted no textures
		// is byte-identical to one written before this key existed.
		if (!document.textureDir.empty())
			json[c_TextureDirKey] = document.textureDir;

		if (document.textureStamp != SourceStamp())
		{
			json[c_TextureStampSizeKey] = document.textureStamp.size;
			json[c_TextureStampHashKey] = document.textureStamp.hash;
		}

		// Omitted rather than written empty, for the same reason textureDir is: a document for a
		// source that produced neither stays byte-identical to one written before these existed.
		if (!document.skeleton.empty())
			json[c_SkeletonKey] = document.skeleton;

		if (!document.outputs.empty())
		{
			auto outputs = std::vector<std::string>(document.outputs);
			std::ranges::sort(outputs);
			json[c_OutputsKey] = std::move(outputs);
		}

		auto bindings = nlohmann::json::object();
		for (const MaterialBinding& binding : document.bindings)
		{
			core::throw_runtime_error_if(
				bindings.contains(binding.submesh),
				"import document: two bindings for submesh '{}'",
				binding.submesh);
			bindings[binding.submesh] = binding.material;
		}
		json[c_BindingsKey] = std::move(bindings);

		const std::string text = doc::canonicalDump(json);

		std::vector<std::byte> bytes(text.size());
		std::memcpy(bytes.data(), text.data(), text.size());
		return bytes;
	}

	uint64_t
	parametersHashOf(const ImportDocument& document)
	{
		return core::hash_string(parametersObject(document).dump(), core::hash_seed());
	}

	ImportDocument
	loadImportDocument(const core::file::IFileSystem& files, std::string_view key)
	{
		const std::vector<std::byte> bytes = files.Read(key);
		return AssetCodec<ImportDocument>::Deserialize(bytes);
	}

	ImportDocument
	loadImportDocument(const std::filesystem::path& path)
	{
		const std::vector<std::byte> bytes = core::file::read_file_bytes(path.string());
		return AssetCodec<ImportDocument>::Deserialize(bytes);
	}

}
