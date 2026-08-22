#include <assetlib/bsky_io.h>
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
#include <core/str/string_pool.h>

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_What = "bsky";

		enum class ChunkId : uint32_t
		{
			kName = 1,
			kBakedPath
		};

		// --- The chunk regime, read until the schema system goes -------------------------------

		constexpr uint16_t c_LegacyVersionMajor = 3;

		enum class LegacyChunkId : uint32_t
		{
			kSky = 1,  // one LegacySkyRecord
			kStringPool
		};

		struct LegacySkyRecord
		{
			uint32_t       nameOffset;
			uint32_t       mipLevel;
			float          rotationY;
			EnvRouteRecord sky;
		};

		static_assert(sizeof(LegacySkyRecord) == 40);

		const schema::Schema&
		legacySkySchema()
		{
			static const schema::Schema c_Schema =
				AssetSchemaBuilder()
					.AddSourceStamp()
					.AddLayout<EnvRouteRecord>("EnvMapRoute", describeEnvRoute)
					.AddLayout<LegacySkyRecord>(
						"SkyRecord",
						[](auto& layout) {
							layout.AddField("nameOffset", &LegacySkyRecord::nameOffset)
								.AddField("mipLevel", &LegacySkyRecord::mipLevel)
								.AddField("rotationY", &LegacySkyRecord::rotationY)
								.AddField("sky", &LegacySkyRecord::sky);
						})
					.Finish();
			return c_Schema;
		}

		LegacySkyRecord
		legacySkyRecord(std::span<const std::byte> bytes, core::string_pool& pool)
		{
			const chunk::Reader
				reader(bytes, magic::c_BSky, c_LegacyVersionMajor, c_What, legacySkySchema());

			const auto records = reader.Require<LegacySkyRecord>(LegacyChunkId::kSky);
			core::throw_runtime_error_if(
				records.size() != 1,
				"bsky: the sky chunk holds {} entries, not one",
				records.size());
			pool = core::string_pool(reader.Read<char>(LegacyChunkId::kStringPool));
			return records[0];
		}

		BSky
		legacyDeserializeSky(std::span<const std::byte> bytes)
		{
			core::string_pool     pool;
			const LegacySkyRecord record = legacySkyRecord(bytes, pool);

			BSky sky;
			sky.name = pool.at(record.nameOffset);
			sky.sky  = unpackRoute(record.sky, pool);
			return sky;
		}
	}

	SkyPresentation
	legacySkyPresentation(std::span<const std::byte> bytes)
	{
		core::string_pool     pool;
		const LegacySkyRecord record = legacySkyRecord(bytes, pool);
		return SkyPresentation{ record.mipLevel, record.rotationY };
	}

	std::vector<std::byte>
	serializeSky(const BSky& sky)
	{
		cache::Writer writer;
		writer.Add(ChunkId::kName, std::span<const char>(sky.name));
		writer.Add(ChunkId::kBakedPath, std::span<const char>(sky.sky.baked));

		// The route *is* the cache key: the source's mount key and the stamp the bake measured.
		return writer.Finish(
			magic::c_BSky,
			c_BSkyBakeToken,
			SourceRef{ sky.sky.source, sky.sky.stamp, 0 });
	}

	BSky
	deserializeSky(std::span<const std::byte> bytes)
	{
		if (!cache::isCacheEntry(bytes))
			return legacyDeserializeSky(bytes);

		const cache::Reader reader(bytes, magic::c_BSky, c_BSkyBakeToken, c_What);

		BSky       sky;
		const auto name  = reader.Read<char>(ChunkId::kName);
		const auto baked = reader.Read<char>(ChunkId::kBakedPath);
		sky.name.assign(name.begin(), name.end());
		sky.sky.baked.assign(baked.begin(), baked.end());
		sky.sky.source = reader.GetSource().key;
		sky.sky.stamp  = reader.GetSource().stamp;
		return sky;
	}

	void
	saveSky(const BSky& sky, const std::filesystem::path& path)
	{
		writeFileBytes(path, serializeSky(sky), "bsky");
	}

	BSky
	loadSky(const std::filesystem::path& path)
	{
		const auto bytes = core::file::read_file_bytes(path.string());
		return deserializeSky(bytes);
	}

	BSky
	loadSky(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return deserializeSky(fileSystem.Read(path));
	}
}
