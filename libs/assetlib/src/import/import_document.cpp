#include <algorithm>
#include <array>
#include <assetlib/ImportIdentity.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/env_import_parameters.h>
#include <assetlib/import_document.h>

#include <charconv>
#include <cmath>
#include <core/err/util.h>
#include <core/file/file.h>
#include <core/hash.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "environment/env_parts.h"
#include "io/json_doc.h"
#include "references/ref_paths.h"
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Mesh.h>

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_ParametersKey        = "parameters";
		constexpr std::string_view c_SampleRateKey        = "sampleRate";
		constexpr std::string_view c_ClipFloorKey         = "clipFloor";
		constexpr std::string_view c_LodMinPixelsKey      = "lodMinPixels";
		constexpr std::string_view c_BindingsKey          = "bindings";
		constexpr std::string_view c_MaterialOverridesKey = "materialOverrides";
		constexpr std::string_view c_TextureDirKey        = "textureDir";
		constexpr std::string_view c_TextureStampSizeKey  = "textureStampSize";
		constexpr std::string_view c_TextureStampHashKey  = "textureStampHash";
		constexpr std::string_view c_PackedSourceSizeKey  = "packedSourceSize";
		constexpr std::string_view c_PackedSourceHashKey  = "packedSourceHash";
		constexpr std::string_view c_TextureBakeTokenKey  = "textureBakeToken";
		constexpr std::string_view c_SkeletonKey          = "skeleton";
		constexpr std::string_view c_ToonShadingRigKey    = "toonShadingRig";
		constexpr std::string_view c_OutputsKey           = "outputs";
		constexpr std::string_view c_SourceKey            = "source";
		constexpr std::string_view c_EnvironmentKey       = "environment";
		constexpr std::string_view c_EnvStampSizeKey      = "envSourceStampSize";
		constexpr std::string_view c_EnvStampHashKey      = "envSourceStampHash";
		constexpr std::string_view c_EnvBakeTokenKey      = "envSourceBakeToken";
		constexpr std::string_view c_EnvSkyHashKey        = "envSkyParametersHash";
		constexpr std::string_view c_EnvLightingHashKey   = "envLightingParametersHash";

		struct EnvironmentField
		{
			std::string_view key;
			uint32_t EnvironmentImportParameters::* field;
		};

		constexpr std::array<EnvironmentField, 6> c_EnvironmentFields = { {
			{ "skyFaceSize", &EnvironmentImportParameters::skyFaceSize },
			{ "skyMips", &EnvironmentImportParameters::skyMips },
			{ "prefilterFaceSize", &EnvironmentImportParameters::prefilterFaceSize },
			{ "prefilterMips", &EnvironmentImportParameters::prefilterMips },
			{ "prefilterSamples", &EnvironmentImportParameters::prefilterSamples },
			{ "irradianceFaceSize", &EnvironmentImportParameters::irradianceFaceSize },
		} };

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

			// Written into whatever the reader kept of the object, so a key a newer branch nested
			// here survives and still reaches the hash.
			if (document.environment)
			{
				auto& environment = parameters[c_EnvironmentKey];
				if (!environment.is_object())
					environment = nlohmann::json::object();
				for (const auto& [key, field] : c_EnvironmentFields)
					environment[key] = (*document.environment).*field;
			}
			else
			{
				parameters[c_SampleRateKey] = doc::plainFloat(document.sampleRate);
			}

			if (!document.clipFloors.empty())
			{
				auto grounds = nlohmann::json::object();
				for (const ClipFloor& ground : document.clipFloors)
				{
					if (grounds.contains(ground.clip))
					{
						core::throw_runtime_error(
							"import document: two authored grounds for clip '{}'",
							ground.clip);
					}
					grounds[ground.clip] = doc::plainFloat(ground.floor);
				}
				parameters[c_ClipFloorKey] = std::move(grounds);
			}

			// Omitted when empty, as clipFloor is: the key must not move for a source that authors
			// no levels.
			if (!document.lodMinPixels.empty())
			{
				auto levels = nlohmann::json::array();
				for (const float minPixels : document.lodMinPixels)
					levels.push_back(doc::plainFloat(minPixels));
				parameters[c_LodMinPixelsKey] = std::move(levels);
			}

			return parameters;
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
	importedSourceKeyFor(std::string_view documentKey, const ImportDocument& document)
	{
		if (!document.source.empty())
			return document.source;

		return swapExtension(documentKey, c_ImportedSourceExtension);
	}

	ImportDocument
	AssetCodec<ImportDocument>::Deserialize(std::span<const std::byte> bytes)
	{
		const auto text =
			std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());

		auto json = doc::parseObject(text, "import document: the document");

		ImportDocument document;

		if (auto identity = json.find("identity"); identity != json.end())
		{
			if (!identity->is_object() || !identity->contains("id") ||
			    !(*identity)["id"].is_string() || !identity->contains("label") ||
			    !(*identity)["label"].is_string())
				core::throw_runtime_error("import document: identity needs id and label strings");
			const auto id = (*identity)["id"].get<std::string>();
			const auto parsed =
				std::from_chars(id.data(), id.data() + id.size(), document.identity.id, 16);
			if (id.size() != 16 || id.find_first_not_of("0123456789abcdef") != std::string::npos ||
			    parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size())
				core::throw_runtime_error(
					"import document: identity id must be 16 lowercase hexadecimal digits");
			document.identity.label = (*identity)["label"].get<std::string>();
			(void)importTextureDirectory(document.identity);
			identity->erase("id");
			identity->erase("label");
			if (identity->empty())
				json.erase(identity);
		}

		if (auto it = json.find(c_ParametersKey); it != json.end())
		{
			if (!it->is_object())
			{
				core::throw_runtime_error(
					"import document: '{}' is not an object",
					c_ParametersKey);
			}
			if (auto rate = it->find(c_SampleRateKey); rate != it->end())
			{
				if (!rate->is_number() || rate->get<float>() <= 0.0f)
				{
					core::throw_runtime_error(
						"import document: '{}' is not a positive number",
						c_SampleRateKey);
				}
				document.sampleRate = rate->get<float>();
				it->erase(rate);
			}
			if (auto grounds = it->find(c_ClipFloorKey); grounds != it->end())
			{
				if (!grounds->is_object())
				{
					core::throw_runtime_error(
						"import document: '{}' is not an object",
						c_ClipFloorKey);
				}
				for (const auto& [clip, floor] : grounds->items())
				{
					if (!floor.is_number())
					{
						core::throw_runtime_error(
							"import document: the authored ground for clip '{}' is not a number",
							clip);
					}
					document.clipFloors.push_back({ clip, floor.get<float>() });
				}
				it->erase(grounds);
			}
			if (auto levels = it->find(c_LodMinPixelsKey); levels != it->end())
			{
				if (!levels->is_array())
				{
					core::throw_runtime_error(
						"import document: '{}' is not an array",
						c_LodMinPixelsKey);
				}
				if (levels->size() > c_MaxMeshLods)
				{
					core::throw_runtime_error(
						"import document: '{}' lists {} levels, more than the {} a mesh may carry",
						c_LodMinPixelsKey,
						levels->size(),
						c_MaxMeshLods);
				}
				float previous = std::numeric_limits<float>::infinity();
				for (const auto& level : *levels)
				{
					if (!level.is_number() || level.get<float>() < 0.0f ||
					    !std::isfinite(level.get<float>()))
					{
						core::throw_runtime_error(
							"import document: '{}' holds an entry that is not a non-negative "
							"number",
							c_LodMinPixelsKey);
					}
					if (level.get<float>() > previous)
					{
						core::throw_runtime_error(
							"import document: '{}' must not increase from one level to the next",
							c_LodMinPixelsKey);
					}
					previous = level.get<float>();
					document.lodMinPixels.push_back(previous);
				}
				it->erase(levels);
			}
			if (auto environment = it->find(c_EnvironmentKey); environment != it->end())
			{
				if (!environment->is_object())
				{
					core::throw_runtime_error(
						"import document: '{}' is not an object",
						c_EnvironmentKey);
				}

				auto parameters = EnvironmentImportParameters();
				for (const auto& [key, field] : c_EnvironmentFields)
				{
					const auto value = environment->find(key);
					if (value == environment->end())
						continue;

					if (!value->is_number_unsigned() || value->get<uint64_t>() == 0 ||
					    value->get<uint64_t>() > std::numeric_limits<uint32_t>::max())
					{
						core::throw_runtime_error(
							"import document: environment '{}' is not a positive 32-bit count",
							key);
					}
					parameters.*field = value->get<uint32_t>();
					environment->erase(value);
				}
				document.environment = parameters;

				if (environment->empty())
					it->erase(environment);
			}

			document.extraParametersJson = it->dump();
			json.erase(it);
		}

		if (auto it = json.find(c_TextureDirKey); it != json.end())
		{
			if (!it->is_string())
			{
				core::throw_runtime_error("import document: '{}' is not a string", c_TextureDirKey);
			}
			document.textureDir = it->get<std::string>();
			json.erase(it);
		}

		for (const auto& [stampKey, field] :
		     { std::pair<std::string_view, uint64_t*>{ c_TextureStampSizeKey,
		                                               &document.textureStamp.size },
		       { c_TextureStampHashKey, &document.textureStamp.hash },
		       { c_PackedSourceSizeKey, &document.packedSourceStamp.size },
		       { c_PackedSourceHashKey, &document.packedSourceStamp.hash },
		       { c_TextureBakeTokenKey, &document.textureBakeToken },
		       { c_EnvStampSizeKey, &document.envSourceStamp.size },
		       { c_EnvStampHashKey, &document.envSourceStamp.hash },
		       { c_EnvBakeTokenKey, &document.envSourceBakeToken },
		       { c_EnvSkyHashKey, &document.envSkyParametersHash },
		       { c_EnvLightingHashKey, &document.envLightingParametersHash } })
		{
			if (const auto it = json.find(stampKey); it != json.end())
			{
				if (!it->is_number_unsigned())
				{
					core::throw_runtime_error(
						"import document: '{}' is not an unsigned number",
						stampKey);
				}
				*field = it->get<uint64_t>();
				json.erase(it);
			}
		}

		for (const auto& [stringKey, field] :
		     { std::pair<std::string_view, std::string*>{ c_SkeletonKey, &document.skeleton },
		       { c_SourceKey, &document.source },
		       { c_ToonShadingRigKey, &document.toonShadingRig } })
		{
			if (const auto it = json.find(stringKey); it != json.end())
			{
				if (!it->is_string())
				{
					core::throw_runtime_error("import document: '{}' is not a string", stringKey);
				}
				*field = it->get<std::string>();
				json.erase(it);
			}
		}

		if (auto it = json.find(c_OutputsKey); it != json.end())
		{
			if (!it->is_array())
			{
				core::throw_runtime_error("import document: '{}' is not an array", c_OutputsKey);
			}
			for (const auto& output : *it)
			{
				if (!output.is_string())
				{
					core::throw_runtime_error(
						"import document: '{}' holds a non-string entry",
						c_OutputsKey);
				}
				document.outputs.push_back(output.get<std::string>());
			}
			json.erase(it);
		}

		if (auto it = json.find(c_BindingsKey); it != json.end())
		{
			if (!it->is_object())
			{
				core::throw_runtime_error("import document: '{}' is not an object", c_BindingsKey);
			}
			for (const auto& [submesh, material] : it->items())
			{
				if (!material.is_string())
				{
					core::throw_runtime_error(
						"import document: binding '{}' is not a string",
						submesh);
				}
				document.bindings.push_back({ submesh, material.get<std::string>() });
			}
			json.erase(it);
		}

		if (auto it = json.find(c_MaterialOverridesKey); it != json.end())
		{
			if (!it->is_object())
			{
				core::throw_runtime_error(
					"import document: '{}' is not an object",
					c_MaterialOverridesKey);
			}
			for (const auto& [submesh, named] : it->items())
			{
				if (!named.is_object())
				{
					core::throw_runtime_error(
						"import document: overrides of '{}' are not an object",
						submesh);
				}
				for (const auto& [name, material] : named.items())
				{
					if (name.empty())
					{
						core::throw_runtime_error(
							"import document: an override of '{}' has no name",
							submesh);
					}
					if (!material.is_string())
					{
						core::throw_runtime_error(
							"import document: override '{}' of '{}' is not a string",
							name,
							submesh);
					}
					document.materialOverrides.push_back(
						{ submesh, name, material.get<std::string>() });
				}
			}
			json.erase(it);
		}

		if (document.environment)
			std::erase_if(document.outputs, [](const std::string& output) {
				return isRetiredEnvironmentOutput(output);
			});

		document.extraJson = json.dump();
		return document;
	}

	std::vector<std::byte>
	AssetCodec<ImportDocument>::Serialize(const ImportDocument& document)
	{
		auto json = doc::parseObject(document.extraJson, "import document: extraJson");

		if (document.identity != ImportIdentity{})
		{
			(void)importTextureDirectory(document.identity);
			auto& identity = json["identity"];
			if (!identity.is_object())
				identity = nlohmann::json::object();
			identity["id"]    = std::format("{:016x}", document.identity.id);
			identity["label"] = document.identity.label;
		}

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
		if (document.packedSourceStamp != SourceStamp())
		{
			json[c_PackedSourceSizeKey] = document.packedSourceStamp.size;
			json[c_PackedSourceHashKey] = document.packedSourceStamp.hash;
		}
		if (document.textureBakeToken != 0)
			json[c_TextureBakeTokenKey] = document.textureBakeToken;

		if (document.envSourceStamp != SourceStamp())
		{
			json[c_EnvStampSizeKey] = document.envSourceStamp.size;
			json[c_EnvStampHashKey] = document.envSourceStamp.hash;
		}
		for (const auto& [key, value] :
		     { std::pair<std::string_view, uint64_t>{ c_EnvBakeTokenKey,
		                                              document.envSourceBakeToken },
		       { c_EnvSkyHashKey, document.envSkyParametersHash },
		       { c_EnvLightingHashKey, document.envLightingParametersHash } })
			if (value != 0)
				json[key] = value;

		// Omitted rather than written empty, for the same reason textureDir is: a document for a
		// source that produced neither stays byte-identical to one written before these existed.
		if (!document.skeleton.empty())
			json[c_SkeletonKey] = document.skeleton;

		if (!document.source.empty())
			json[c_SourceKey] = document.source;

		if (!document.toonShadingRig.empty())
			json[c_ToonShadingRigKey] = document.toonShadingRig;

		if (!document.outputs.empty())
		{
			auto outputs = std::vector<std::string>(document.outputs);
			std::ranges::sort(outputs);
			json[c_OutputsKey] = std::move(outputs);
		}

		auto bindings = nlohmann::json::object();
		for (const MaterialBinding& binding : document.bindings)
		{
			if (bindings.contains(binding.submesh))
			{
				core::throw_runtime_error(
					"import document: two bindings for submesh '{}'",
					binding.submesh);
			}
			bindings[binding.submesh] = binding.material;
		}
		json[c_BindingsKey] = std::move(bindings);

		// Omitted rather than written empty, so a document with none stays byte-identical.
		if (!document.materialOverrides.empty())
		{
			auto overrides = nlohmann::json::object();
			for (const MaterialOverrideBinding& entry : document.materialOverrides)
			{
				if (entry.name.empty())
				{
					core::throw_runtime_error(
						"import document: an override of '{}' has no name",
						entry.submesh);
				}
				nlohmann::json& named = overrides[entry.submesh];
				if (named.contains(entry.name))
				{
					core::throw_runtime_error(
						"import document: two overrides named '{}' for submesh '{}'",
						entry.name,
						entry.submesh);
				}
				named[entry.name] = entry.material;
			}
			json[c_MaterialOverridesKey] = std::move(overrides);
		}

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
