#include <assetlib/banim_io.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Skeleton.h>

#include <assetlib/skeleton.h>

#include "cache_io.h"
#include "fs_util.h"

#include <assetlib_structs/magic.h>
#include <core/err/util.h>

#include <core/file/file.h>

#include "mounted_io.h"

namespace assetlib
{
	using core::throw_runtime_error;

	namespace
	{
		constexpr std::string_view c_What = "banim";

		enum class ChunkId : uint32_t
		{
			kClips = 1,
			kSamples,
			kStringPool,
			kSkeletonRef,   // the signature and bone count the clips were cooked against
			kSkeletonPath,  // the .bskel path
			kPosedBoxes     // PosedBox entries: the posed culling boxes this file carries
		};

		/** What the clips were cooked against, so a rig that has changed since is refused. */
		struct SkeletonRef
		{
			uint64_t signature;
			uint32_t boneCount;
		};

		static_assert(sizeof(SkeletonRef) == 16);

		std::vector<SkeletonRef>
		packSkeletonRef(const AnimationSet& animations)
		{
			SkeletonRef ref{};
			ref.signature = animations.skeletonSignature;
			ref.boneCount = animations.boneCount;
			return { ref };
		}

		void
		unpackSkeletonRef(
			AnimationSet&                animations,
			std::span<const SkeletonRef> ref,
			std::span<const char>        path)
		{
			core::throw_runtime_error_if(
				ref.size() > 1,
				"banim: the skeleton reference chunk holds {} entries",
				ref.size());
			if (!ref.empty())
			{
				animations.skeletonSignature = ref[0].signature;
				animations.boneCount         = ref[0].boneCount;
			}
			animations.skeleton.assign(path.begin(), path.end());
		}
	}

	std::vector<std::byte>
	serializeAnimations(const AnimationSet& animations)
	{
		cache::Writer writer;
		writer.Add(ChunkId::kClips, animations.clips);
		writer.Add(ChunkId::kSamples, animations.samples);
		writer.Add(ChunkId::kStringPool, animations.stringPool.bytes());
		writer.Add(ChunkId::kSkeletonRef, packSkeletonRef(animations));
		writer.Add(ChunkId::kSkeletonPath, std::span<const char>(animations.skeleton));
		if (!animations.posedBoxes.empty())
			writer.Add(ChunkId::kPosedBoxes, animations.posedBoxes);
		return writer.Finish(
			magic::c_BAnim,
			AssetCodec<AnimationSet>::c_BakeToken,
			animations.source);
	}

	AnimationSet
	deserializeAnimations(std::span<const std::byte> bytes)
	{
		const cache::Reader reader(
			bytes,
			magic::c_BAnim,
			AssetCodec<AnimationSet>::c_BakeToken,
			c_What);

		AnimationSet animations;
		animations.source     = reader.GetSource();
		animations.clips      = reader.Read<AnimationClip>(ChunkId::kClips);
		animations.samples    = reader.Read<Transform>(ChunkId::kSamples);
		animations.stringPool = core::string_pool(reader.Read<char>(ChunkId::kStringPool));
		animations.posedBoxes = reader.Read<PosedBox>(ChunkId::kPosedBoxes);
		unpackSkeletonRef(
			animations,
			reader.Read<SkeletonRef>(ChunkId::kSkeletonRef),
			reader.Read<char>(ChunkId::kSkeletonPath));

		validateAnimationSet(animations);
		return animations;
	}

	void
	saveAnimations(const AnimationSet& animations, const std::filesystem::path& path)
	{
		writeFileBytes(path, serializeAnimations(animations), "banim");
	}

	AnimationSet
	loadAnimations(const std::filesystem::path& path)
	{
		return deserializeAnimations(core::file::read_file_bytes(path.string()));
	}

	namespace
	{
		constexpr std::array<uint32_t, 1> c_WantedRefChunks = { { uint32_t(
			ChunkId::kSkeletonPath) } };

		std::string
		skeletonPathFromChunks(const cache::CacheData& chunks)
		{
			const auto path = chunks.Read<char>(ChunkId::kSkeletonPath, c_What);
			return std::string(path.begin(), path.end());
		}
	}

	std::string
	loadAnimationSkeletonPath(const std::filesystem::path& path)
	{
		return skeletonPathFromChunks(
			cache::readCacheChunksFromFile(
				path,
				magic::c_BAnim,
				AssetCodec<AnimationSet>::c_BakeToken,
				c_WantedRefChunks,
				c_What));
	}

	std::string
	loadAnimationSkeletonPath(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return skeletonPathFromChunks(
			cache::readCacheChunksFrom(
				fileSystem,
				path,
				magic::c_BAnim,
				AssetCodec<AnimationSet>::c_BakeToken,
				c_WantedRefChunks,
				c_What));
	}

	bool
	animationsMatchSkeleton(const AnimationSet& animations, const Skeleton& skeleton) noexcept
	{
		return animations.boneCount == skeleton.bones.size() &&
		       animations.skeletonSignature == skeletonSignature(skeleton);
	}

	std::vector<std::byte>
	AssetCodec<AnimationSet>::Serialize(const AnimationSet& value)
	{
		return serializeAnimations(value);
	}

	AnimationSet
	AssetCodec<AnimationSet>::Deserialize(std::span<const std::byte> bytes)
	{
		return deserializeAnimations(bytes);
	}
}
