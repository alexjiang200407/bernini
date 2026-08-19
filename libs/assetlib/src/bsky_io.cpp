#include <assetlib/bsky_io.h>
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

		constexpr std::string_view c_What = "bsky";

		enum class ChunkId : uint32_t
		{
			kSky = 1,  // one SkyRecord
			kStringPool
		};

		struct SkyRecord
		{
			uint32_t       nameOffset;
			uint32_t       mipLevel;
			float          rotationY;
			uint32_t       pad;
			EnvRouteRecord sky;
		};

		static_assert(sizeof(SkyRecord) == 40);

		const schema::Schema&
		skySchema()
		{
			static const schema::Schema c_Schema =
				AssetSchemaBuilder()
					.AddSourceStamp()
					.AddLayout<EnvRouteRecord>("EnvMapRoute", describeEnvRoute)
					.AddLayout<SkyRecord>(
						"SkyRecord",
						[](auto& layout) {
							layout.AddField("nameOffset", &SkyRecord::nameOffset)
								.AddField("mipLevel", &SkyRecord::mipLevel)
								.AddField("rotationY", &SkyRecord::rotationY)
								.AddField("pad", &SkyRecord::pad)
								.AddField("sky", &SkyRecord::sky);
						})
					.Finish();
			return c_Schema;
		}
	}

	std::vector<std::byte>
	serializeSky(const BSky& sky)
	{
		core::string_pool pool;
		SkyRecord         record{};
		record.nameOffset = pool.add(sky.name);
		record.mipLevel   = sky.mipLevel;
		record.rotationY  = sky.rotationY;
		record.sky        = packRoute(sky.sky, pool);

		chunk::Writer writer(skySchema());
		writer.Add(ChunkId::kSky, std::vector<SkyRecord>{ record });
		writer.Add(ChunkId::kStringPool, pool.bytes());
		return writer.Finish(magic::c_BSky, c_VersionMajor, c_VersionMinor);
	}

	BSky
	deserializeSky(std::span<const std::byte> bytes)
	{
		const chunk::Reader reader(bytes, magic::c_BSky, c_VersionMajor, c_What, skySchema());

		const auto records = reader.Require<SkyRecord>(ChunkId::kSky);
		core::throw_runtime_error_if(
			records.size() != 1,
			"bsky: the sky chunk holds {} entries, not one",
			records.size());
		const core::string_pool pool(reader.Read<char>(ChunkId::kStringPool));

		BSky sky;
		sky.name      = pool.at(records[0].nameOffset);
		sky.mipLevel  = records[0].mipLevel;
		sky.rotationY = records[0].rotationY;
		sky.sky       = unpackRoute(records[0].sky, pool);
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
