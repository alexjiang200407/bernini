#include <assetlib/codecs.h>
#include <assetlib/import_document.h>

#include <assetlib/container_format.h>
#include <core/err/util.h>
#include <core/file/file.h>
#include <core/hash.h>
#include <nlohmann/json.hpp>

#include "json_doc.h"
#include "ref_paths.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_ParametersKey = "parameters";
		constexpr std::string_view c_SampleRateKey = "sampleRate";
		constexpr std::string_view c_BindingsKey   = "bindings";

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
	importDocumentKeyFor(std::string_view sourceKey)
	{
		return swapExtension(sourceKey, c_ImportDocumentExtension);
	}

	std::string
	importedSourceKeyFor(std::string_view documentKey)
	{
		return swapExtension(documentKey, ".glb");
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
			document.extraParametersJson = it->dump();
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

		auto parameters =
			doc::parseObject(document.extraParametersJson, "import document: extraParametersJson");
		parameters[c_SampleRateKey] = doc::plainFloat(document.sampleRate);
		json[c_ParametersKey]       = std::move(parameters);

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
		auto parameters =
			doc::parseObject(document.extraParametersJson, "import document: extraParametersJson");
		parameters[c_SampleRateKey] = doc::plainFloat(document.sampleRate);
		return core::hash_string(parameters.dump(), core::hash_seed());
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
