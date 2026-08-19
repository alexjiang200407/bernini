#include "assetlib/benv_io.h"

#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>
#include <core/err/util.h>
#include <core/file/file.h>
#include <core/str/string_pool.h>

#include "AssetSchemaBuilder.h"
#include "chunk_io.h"
#include "fs_util.h"

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		constexpr uint16_t c_EnvVersionMajor = 3;
		constexpr uint16_t c_EnvVersionMinor = 0;

		constexpr std::string_view c_What = "benv";

		enum class ChunkId : uint32_t
		{
			kEnv = 1,  // one EnvRecord
			kStringPool
		};

		struct EnvRecord
		{
			uint32_t nameOffset;
			uint32_t skyOffset;
			uint32_t lightingOffset;
		};

		static_assert(sizeof(EnvRecord) == 12);

		const schema::Schema&
		envSchema()
		{
			static const schema::Schema c_Schema =
				schema::SchemaBuilder()
					.AddLayout<EnvRecord>(
						"EnvRecord",
						[](auto& layout) {
							layout.AddField("nameOffset", &EnvRecord::nameOffset)
								.AddField("skyOffset", &EnvRecord::skyOffset)
								.AddField("lightingOffset", &EnvRecord::lightingOffset);
						})
					.Finish();
			return c_Schema;
		}
	}

	std::vector<std::byte>
	serializeEnv(const BEnv& env)
	{
		core::string_pool pool;
		EnvRecord         record{};
		record.nameOffset     = pool.add(env.name);
		record.skyOffset      = pool.add(env.sky);
		record.lightingOffset = pool.add(env.lighting);

		chunk::Writer writer(envSchema());
		writer.Add(ChunkId::kEnv, std::vector<EnvRecord>{ record });
		writer.Add(ChunkId::kStringPool, pool.bytes());
		return writer.Finish(magic::c_BEnv, c_EnvVersionMajor, c_EnvVersionMinor);
	}

	BEnv
	deserializeEnv(std::span<const std::byte> bytes)
	{
		const chunk::Reader reader(bytes, magic::c_BEnv, c_EnvVersionMajor, c_What, envSchema());

		const auto records = reader.Require<EnvRecord>(ChunkId::kEnv);
		core::throw_runtime_error_if(
			records.size() != 1,
			"benv: the env chunk holds {} entries, not one",
			records.size());
		const core::string_pool pool(reader.Read<char>(ChunkId::kStringPool));

		BEnv env;
		env.name     = pool.at(records[0].nameOffset);
		env.sky      = pool.at(records[0].skyOffset);
		env.lighting = pool.at(records[0].lightingOffset);
		return env;
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
