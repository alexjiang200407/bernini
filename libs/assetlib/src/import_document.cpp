#include <assetlib/import_document.h>

#include <assetlib/container_format.h>
#include <core/err/util.h>
#include <nlohmann/json.hpp>

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

		nlohmann::json
		parseObject(std::string_view text, std::string_view what)
		{
			auto json = nlohmann::json::parse(text, nullptr, false);
			core::throw_runtime_error_if(
				json.is_discarded() || !json.is_object(),
				"import document: {} is not a JSON object",
				what);
			return json;
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
	deserializeImportDocument(std::string_view text)
	{
		auto json = parseObject(text, "the document");

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

	std::string
	serializeImportDocument(const ImportDocument& document)
	{
		auto json = parseObject(document.extraJson, "extraJson");

		auto parameters = parseObject(document.extraParametersJson, "extraParametersJson");
		parameters[c_SampleRateKey] = document.sampleRate;
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

		return json.dump(1, '\t') + '\n';
	}

	ImportDocument
	loadImportDocument(const core::file::IFileSystem& files, std::string_view key)
	{
		const std::vector<std::byte> bytes = files.Read(key);
		return deserializeImportDocument(
			std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
	}
}
