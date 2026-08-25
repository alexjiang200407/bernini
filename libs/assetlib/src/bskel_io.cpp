#include <assetlib/codecs.h>
#include <assetlib_structs/Skeleton.h>

#include <assetlib/skinning.h>

#include "cache_io.h"
#include "fs_util.h"

#include <assetlib_structs/magic.h>

#include <core/file/file.h>

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_What = "bskel";

		enum class ChunkId : uint32_t
		{
			kBones = 1,
			kStringPool
		};

	}

	std::vector<std::byte>
	AssetCodec<Skeleton>::Serialize(const Skeleton& skeleton)
	{
		cache::Writer writer;
		writer.Add(ChunkId::kBones, skeleton.bones);
		writer.Add(ChunkId::kStringPool, skeleton.stringPool.bytes());
		return writer.Finish(magic::c_BSkel, AssetCodec<Skeleton>::c_BakeToken, skeleton.source);
	}

	Skeleton
	AssetCodec<Skeleton>::Deserialize(std::span<const std::byte> bytes)
	{
		const cache::Reader reader(
			bytes,
			magic::c_BSkel,
			AssetCodec<Skeleton>::c_BakeToken,
			c_What);

		Skeleton skeleton;
		skeleton.source     = reader.GetSource();
		skeleton.bones      = reader.Require<Bone>(ChunkId::kBones);
		skeleton.stringPool = core::string_pool(reader.Read<char>(ChunkId::kStringPool));

		validateSkeleton(skeleton);
		return skeleton;
	}

}
