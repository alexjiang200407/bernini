#include <assetlib/bsky_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>

#include "bake_tokens.h"
#include "cache_io.h"
#include "fs_util.h"

#include <core/file/file.h>

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

	std::vector<std::byte>
	AssetCodec<BSky>::Serialize(const BSky& value)
	{
		return serializeSky(value);
	}

	BSky
	AssetCodec<BSky>::Deserialize(std::span<const std::byte> bytes)
	{
		return deserializeSky(bytes);
	}
}
