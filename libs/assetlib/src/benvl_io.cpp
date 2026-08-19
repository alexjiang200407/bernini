#include <assetlib/benvl_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>

#include "AssetSchemaBuilder.h"
#include "chunk_io.h"
#include "env_route_io.h"
#include "fs_util.h"

#include <core/err/util.h>
#include <core/file/file.h>
#include <core/str/string_pool.h>

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		constexpr uint16_t c_VersionMajor = 3;  // 3: a chunk container with a schema
		constexpr uint16_t c_VersionMinor = 0;

		constexpr std::string_view c_What = "benvl";

		enum class ChunkId : uint32_t
		{
			kLighting = 1,  // one LightingRecord
			kStringPool
		};

		struct LightingRecord
		{
			uint32_t       nameOffset;
			float          exposure;
			uint32_t       exposureAuthored;  // 1 when exposureOverride is set
			float          exposureOverride;
			EnvRouteRecord prefilter;
			EnvRouteRecord irradiance;
		};

		static_assert(sizeof(LightingRecord) == 64);

		const schema::Schema&
		envLightingSchema()
		{
			static const schema::Schema c_Schema =
				AssetSchemaBuilder()
					.AddSourceStamp()
					.AddLayout<EnvRouteRecord>("EnvMapRoute", describeEnvRoute)
					.AddLayout<LightingRecord>(
						"LightingRecord",
						[](auto& layout) {
							layout.AddField("nameOffset", &LightingRecord::nameOffset)
								.AddField("exposure", &LightingRecord::exposure, 1.0f)
								.AddField("exposureAuthored", &LightingRecord::exposureAuthored)
								.AddField("exposureOverride", &LightingRecord::exposureOverride)
								.AddField("prefilter", &LightingRecord::prefilter)
								.AddField("irradiance", &LightingRecord::irradiance);
						})
					.Finish();
			return c_Schema;
		}
	}

	std::vector<std::byte>
	serializeEnvLighting(const BEnvLighting& lighting)
	{
		core::string_pool pool;
		LightingRecord    record{};
		record.nameOffset       = pool.add(lighting.name);
		record.exposure         = lighting.exposure;
		record.exposureAuthored = lighting.exposureOverride.has_value() ? 1u : 0u;
		record.exposureOverride = lighting.exposureOverride.value_or(0.0f);
		record.prefilter        = packRoute(lighting.prefilter, pool);
		record.irradiance       = packRoute(lighting.irradiance, pool);

		chunk::Writer writer(envLightingSchema());
		writer.Add(ChunkId::kLighting, std::vector<LightingRecord>{ record });
		writer.Add(ChunkId::kStringPool, pool.bytes());
		return writer.Finish(magic::c_BEnvL, c_VersionMajor, c_VersionMinor);
	}

	BEnvLighting
	deserializeEnvLighting(std::span<const std::byte> bytes)
	{
		const chunk::Reader
			reader(bytes, magic::c_BEnvL, c_VersionMajor, c_What, envLightingSchema());

		const auto records = reader.Require<LightingRecord>(ChunkId::kLighting);
		core::throw_runtime_error_if(
			records.size() != 1,
			"benvl: the lighting chunk holds {} entries, not one",
			records.size());
		const core::string_pool pool(reader.Read<char>(ChunkId::kStringPool));

		const LightingRecord& record = records[0];
		BEnvLighting          lighting;
		lighting.name       = pool.at(record.nameOffset);
		lighting.exposure   = record.exposure;
		lighting.prefilter  = unpackRoute(record.prefilter, pool);
		lighting.irradiance = unpackRoute(record.irradiance, pool);
		if (record.exposureAuthored != 0)
			lighting.exposureOverride = record.exposureOverride;
		return lighting;
	}

	void
	saveEnvLighting(const BEnvLighting& lighting, const std::filesystem::path& path)
	{
		writeFileBytes(path, serializeEnvLighting(lighting), "benvl");
	}

	BEnvLighting
	loadEnvLighting(const std::filesystem::path& path)
	{
		const auto bytes = core::file::read_file_bytes(path.string());
		return deserializeEnvLighting(bytes);
	}

	BEnvLighting
	loadEnvLighting(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return deserializeEnvLighting(fileSystem.Read(path));
	}
}
