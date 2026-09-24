#include <array>
#include <assetlib/codecs.h>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/magic.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cache_io.h"
#include "mounted_io.h"
#include <core/file/IFileSystem.h>

namespace assetlib
{
	using core::throw_runtime_error_if;

	namespace
	{
		constexpr std::string_view c_What = "bgrassfields";

		enum class ChunkId : uint32_t
		{
			kFields = 1,
			kNames,
			kLooks,
			kChunks,
			kClumps,
		};

		/**
		 * Refuses a field, chunk or look slot that addresses past the pool it names. The file is
		 * the only evidence of its own ranges, so they are checked before anything trusts them.
		 */
		void
		validateGrassFields(const BGrassFields& grass)
		{
			throw_runtime_error_if(
				grass.names.size() != grass.fields.size(),
				"bgrassfields: {} fields but {} names",
				grass.fields.size(),
				grass.names.size());

			for (size_t f = 0; f < grass.fields.size(); ++f)
			{
				const GrassField& field = grass.fields[f];
				throw_runtime_error_if(
					field.look != c_InvalidIndex && field.look >= grass.looks.size(),
					"bgrassfields: field {} draws with look slot {}, past the {} it holds",
					f,
					field.look,
					grass.looks.size());
				throw_runtime_error_if(
					field.chunkCount == 0 ||
						static_cast<uint64_t>(field.firstChunk) + field.chunkCount >
							grass.chunks.size(),
					"bgrassfields: field {} addresses chunks {}+{}, outside the {} there are",
					f,
					field.firstChunk,
					field.chunkCount,
					grass.chunks.size());
			}

			for (size_t c = 0; c < grass.chunks.size(); ++c)
			{
				const GrassChunk& chunk = grass.chunks[c];
				throw_runtime_error_if(
					chunk.clumpCount == 0 || chunk.clumpCount > c_GrassClumpsPerChunk ||
						static_cast<uint64_t>(chunk.firstClump) + chunk.clumpCount >
							grass.clumps.size(),
					"bgrassfields: chunk {} addresses clumps {}+{}, outside the {} there are or "
					"more than {}",
					c,
					chunk.firstClump,
					chunk.clumpCount,
					grass.clumps.size(),
					c_GrassClumpsPerChunk);
			}
		}
	}

	std::vector<std::byte>
	AssetCodec<BGrassFields>::Serialize(const BGrassFields& grass)
	{
		validateGrassFields(grass);

		cache::Writer writer;
		writer.Add(ChunkId::kFields, grass.fields);
		writer.Add(ChunkId::kNames, cache::packStrings(grass.names));
		writer.Add(ChunkId::kLooks, cache::packStrings(grass.looks));
		writer.Add(ChunkId::kChunks, grass.chunks);
		writer.Add(ChunkId::kClumps, grass.clumps);
		return writer.Finish(magic::c_BGrassF, AssetCodec<BGrassFields>::c_BakeToken, grass.source);
	}

	BGrassFields
	AssetCodec<BGrassFields>::Deserialize(std::span<const std::byte> bytes)
	{
		const cache::Reader reader(
			bytes,
			magic::c_BGrassF,
			AssetCodec<BGrassFields>::c_BakeToken,
			c_What);

		BGrassFields grass;
		grass.source = reader.GetSource();
		grass.fields = reader.Read<GrassField>(ChunkId::kFields);
		grass.names  = cache::unpackStrings(reader.Read<char>(ChunkId::kNames));
		grass.looks  = cache::unpackStrings(reader.Read<char>(ChunkId::kLooks));
		grass.chunks = reader.Read<GrassChunk>(ChunkId::kChunks);
		grass.clumps = reader.Read<GrassClump>(ChunkId::kClumps);

		validateGrassFields(grass);
		return grass;
	}

	std::vector<std::string>
	loadGrassLooks(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		constexpr std::array<uint32_t, 1> c_Wanted = { { static_cast<uint32_t>(ChunkId::kLooks) } };

		const cache::CacheData chunks = cache::readCacheChunksFrom(
			fileSystem,
			path,
			magic::c_BGrassF,
			AssetCodec<BGrassFields>::c_BakeToken,
			c_Wanted,
			c_What);
		return cache::unpackStrings(chunks.Read<char>(ChunkId::kLooks, c_What));
	}
}
