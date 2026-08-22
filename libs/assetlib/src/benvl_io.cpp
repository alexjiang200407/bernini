#include <assetlib/benvl_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>

#include "AssetSchemaBuilder.h"
#include "bake_tokens.h"
#include "cache_io.h"
#include "chunk_io.h"
#include "env_legacy.h"
#include "env_route_io.h"
#include "fs_util.h"

#include <core/err/util.h>
#include <core/file/file.h>
#include <core/str/str.h>
#include <core/str/string_pool.h>

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_What = "benvl";

		enum class ChunkId : uint32_t
		{
			kName = 1,
			kPrefilterBaked,
			kIrradianceBaked,
			kIrradianceStamp,  // one SourceStamp; the prefilter's rides the header
			kExposure          // one float, the bake's derivation
		};

		// The two convolutions have two sources, and the header carries one key -- so the key is
		// both, joined. '\n' cannot appear in a mount key, which is what makes the join safe.
		constexpr char c_KeySeparator = '\n';

		// --- The chunk regime, read until the schema system goes -------------------------------

		constexpr uint16_t c_LegacyVersionMajor = 3;

		enum class LegacyChunkId : uint32_t
		{
			kLighting = 1,  // one LegacyLightingRecord
			kStringPool
		};

		struct LegacyLightingRecord
		{
			uint32_t       nameOffset;
			float          exposure;
			uint32_t       exposureAuthored;  // 1 when an override was set
			float          exposureOverride;
			EnvRouteRecord prefilter;
			EnvRouteRecord irradiance;
		};

		static_assert(sizeof(LegacyLightingRecord) == 64);

		const schema::Schema&
		legacyLightingSchema()
		{
			static const schema::Schema c_Schema =
				AssetSchemaBuilder()
					.AddSourceStamp()
					.AddLayout<EnvRouteRecord>("EnvMapRoute", describeEnvRoute)
					.AddLayout<LegacyLightingRecord>(
						"LightingRecord",
						[](auto& layout) {
							layout.AddField("nameOffset", &LegacyLightingRecord::nameOffset)
								.AddField("exposure", &LegacyLightingRecord::exposure, 1.0f)
								.AddField(
									"exposureAuthored",
									&LegacyLightingRecord::exposureAuthored)
								.AddField(
									"exposureOverride",
									&LegacyLightingRecord::exposureOverride)
								.AddField("prefilter", &LegacyLightingRecord::prefilter)
								.AddField("irradiance", &LegacyLightingRecord::irradiance);
						})
					.Finish();
			return c_Schema;
		}

		LegacyLightingRecord
		legacyLightingRecord(std::span<const std::byte> bytes, core::string_pool& pool)
		{
			const chunk::Reader
				reader(bytes, magic::c_BEnvL, c_LegacyVersionMajor, c_What, legacyLightingSchema());

			const auto records = reader.Require<LegacyLightingRecord>(LegacyChunkId::kLighting);
			core::throw_runtime_error_if(
				records.size() != 1,
				"benvl: the lighting chunk holds {} entries, not one",
				records.size());
			pool = core::string_pool(reader.Read<char>(LegacyChunkId::kStringPool));
			return records[0];
		}

		BEnvLighting
		legacyDeserializeEnvLighting(std::span<const std::byte> bytes)
		{
			core::string_pool          pool;
			const LegacyLightingRecord record = legacyLightingRecord(bytes, pool);

			BEnvLighting lighting;
			lighting.name       = pool.at(record.nameOffset);
			lighting.exposure   = record.exposure;
			lighting.prefilter  = unpackRoute(record.prefilter, pool);
			lighting.irradiance = unpackRoute(record.irradiance, pool);
			return lighting;
		}
	}

	std::optional<float>
	legacyLightingExposureOverride(std::span<const std::byte> bytes)
	{
		core::string_pool          pool;
		const LegacyLightingRecord record = legacyLightingRecord(bytes, pool);
		if (record.exposureAuthored == 0)
			return std::nullopt;
		return record.exposureOverride;
	}

	std::vector<std::byte>
	serializeEnvLighting(const BEnvLighting& lighting)
	{
		// Both sources, or neither: a one-sided pair written silently would drop the recorded
		// half from the key, and the entry would then read as current-without-source forever.
		core::throw_runtime_error_if(
			lighting.prefilter.source.empty() != lighting.irradiance.source.empty(),
			"benvl: one convolution routes a source and the other does not; the pair bakes "
			"together or not at all");

		cache::Writer writer;
		writer.Add(ChunkId::kName, std::span<const char>(lighting.name));
		writer.Add(ChunkId::kPrefilterBaked, std::span<const char>(lighting.prefilter.baked));
		writer.Add(ChunkId::kIrradianceBaked, std::span<const char>(lighting.irradiance.baked));
		writer.Add(
			ChunkId::kIrradianceStamp,
			std::span<const SourceStamp>(&lighting.irradiance.stamp, 1));
		writer.Add(ChunkId::kExposure, std::span<const float>(&lighting.exposure, 1));

		SourceRef source;
		if (!lighting.prefilter.source.empty())
		{
			source.key   = lighting.prefilter.source + c_KeySeparator + lighting.irradiance.source;
			source.stamp = lighting.prefilter.stamp;
		}
		return writer.Finish(magic::c_BEnvL, c_BEnvLightingBakeToken, source);
	}

	BEnvLighting
	deserializeEnvLighting(std::span<const std::byte> bytes)
	{
		if (!cache::isCacheEntry(bytes))
			return legacyDeserializeEnvLighting(bytes);

		const cache::Reader reader(bytes, magic::c_BEnvL, c_BEnvLightingBakeToken, c_What);

		BEnvLighting lighting;
		const auto   name            = reader.Read<char>(ChunkId::kName);
		const auto   prefilterBaked  = reader.Read<char>(ChunkId::kPrefilterBaked);
		const auto   irradianceBaked = reader.Read<char>(ChunkId::kIrradianceBaked);
		lighting.name.assign(name.begin(), name.end());
		lighting.prefilter.baked.assign(prefilterBaked.begin(), prefilterBaked.end());
		lighting.irradiance.baked.assign(irradianceBaked.begin(), irradianceBaked.end());

		const auto irradianceStamp = reader.Read<SourceStamp>(ChunkId::kIrradianceStamp);
		if (!irradianceStamp.empty())
			lighting.irradiance.stamp = irradianceStamp.front();

		const auto exposure = reader.Read<float>(ChunkId::kExposure);
		if (!exposure.empty())
			lighting.exposure = exposure.front();

		const SourceRef& source = reader.GetSource();
		if (source.key.find(c_KeySeparator) != std::string::npos)
		{
			const auto [prefilter, irradiance] = core::str::split_once(source.key, "\n");
			lighting.prefilter.source          = prefilter;
			lighting.irradiance.source         = irradiance;
			lighting.prefilter.stamp           = source.stamp;
		}
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
