#include "assetlib/benv_io.h"

#include <assetlib/container_info.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>
#include <core/err/util.h>
#include <core/file/file.h>
#include <core/str/string_pool.h>
#include <nlohmann/json.hpp>

#include "AssetSchemaBuilder.h"
#include "chunk_io.h"
#include "fs_util.h"
#include "json_doc.h"

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_What = "benv";

		// --- The chunk regime, read until the schema system goes -------------------------------

		constexpr uint16_t c_LegacyVersionMajor = 3;

		enum class LegacyChunkId : uint32_t
		{
			kEnv = 1,  // one LegacyEnvRecord
			kStringPool
		};

		struct LegacyEnvRecord
		{
			uint32_t nameOffset;
			uint32_t skyOffset;
			uint32_t lightingOffset;
		};

		static_assert(sizeof(LegacyEnvRecord) == 12);

		const schema::Schema&
		legacyEnvSchema()
		{
			static const schema::Schema c_Schema =
				schema::SchemaBuilder()
					.AddLayout<LegacyEnvRecord>(
						"EnvRecord",
						[](auto& layout) {
							layout.AddField("nameOffset", &LegacyEnvRecord::nameOffset)
								.AddField("skyOffset", &LegacyEnvRecord::skyOffset)
								.AddField("lightingOffset", &LegacyEnvRecord::lightingOffset);
						})
					.Finish();
			return c_Schema;
		}

		BEnv
		legacyDeserializeEnv(std::span<const std::byte> bytes)
		{
			const chunk::Reader
				reader(bytes, magic::c_BEnv, c_LegacyVersionMajor, c_What, legacyEnvSchema());

			const auto records = reader.Require<LegacyEnvRecord>(LegacyChunkId::kEnv);
			core::throw_runtime_error_if(
				records.size() != 1,
				"benv: the env chunk holds {} entries, not one",
				records.size());
			const core::string_pool pool(reader.Read<char>(LegacyChunkId::kStringPool));

			BEnv env;
			env.name     = pool.at(records[0].nameOffset);
			env.sky      = pool.at(records[0].skyOffset);
			env.lighting = pool.at(records[0].lightingOffset);
			return env;
		}

		BEnv
		envFromDocument(std::string_view text)
		{
			auto json = doc::parseObject(text, "benv: the document");

			BEnv env;
			doc::take(json, "name", env.name, c_What);
			doc::take(json, "sky", env.sky, c_What);
			doc::take(json, "lighting", env.lighting, c_What);
			doc::take(json, "skyMipLevel", env.skyMipLevel, c_What);
			doc::take(json, "skyRotationY", env.skyRotationY, c_What);

			if (json.contains("exposureOverride"))
			{
				float authored = 0.0f;
				doc::take(json, "exposureOverride", authored, c_What);
				env.exposureOverride = authored;
			}

			env.extraJson = json.dump();
			return env;
		}
	}

	std::vector<std::byte>
	serializeEnv(const BEnv& env)
	{
		auto json = doc::parseObject(env.extraJson, "benv: extraJson");

		json["name"]         = env.name;
		json["sky"]          = env.sky;
		json["lighting"]     = env.lighting;
		json["skyMipLevel"]  = env.skyMipLevel;
		json["skyRotationY"] = doc::plainFloat(env.skyRotationY);

		if (env.exposureOverride.has_value())
			json["exposureOverride"] = doc::plainFloat(*env.exposureOverride);
		else
			json.erase("exposureOverride");

		return doc::toBytes(json);
	}

	BEnv
	deserializeEnv(std::span<const std::byte> bytes)
	{
		if (isTextAssetDocument(bytes))
			return envFromDocument(
				std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));

		// The chunk regime, read until the schema system goes.
		return legacyDeserializeEnv(bytes);
	}

	void
	saveEnv(const BEnv& env, const std::filesystem::path& path)
	{
		writeFileBytes(path, serializeEnv(env), "benv");
	}

	BEnv
	loadEnv(const std::filesystem::path& path)
	{
		const auto bytes = core::file::read_file_bytes(path.string());
		return deserializeEnv(bytes);
	}

	BEnv
	loadEnv(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return deserializeEnv(fileSystem.Read(path));
	}
}
