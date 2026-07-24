#include <assetlib/banim_io.h>

#include <assetlib/skeleton.h>

#include "chunk_io.h"
#include "fs_util.h"

#include <core/file/file.h>

namespace assetlib
{
	namespace
	{
		constexpr uint32_t c_Magic = 0x4D4E4142u;  // 'B','A','N','M' little-endian

		constexpr uint16_t c_VersionMajor = 1;
		constexpr uint16_t c_VersionMinor = 0;

		constexpr std::string_view c_What = "banim";

		enum ChunkId : uint32_t
		{
			kClips = 1,
			kSamples,
			kStringPool,
			kSkeletonRef  // the .bskel path, then the signature and bone count it was cooked against
		};

		struct SkeletonRef
		{
			uint64_t signature;
			uint32_t boneCount;
			uint32_t pathLength;
		};

		static_assert(sizeof(SkeletonRef) == 16);

		std::vector<std::byte>
		packSkeletonRef(const AnimationSet& animations)
		{
			SkeletonRef ref{};
			ref.signature  = animations.skeletonSignature;
			ref.boneCount  = animations.boneCount;
			ref.pathLength = static_cast<uint32_t>(animations.skeleton.size());

			std::vector<std::byte> out(sizeof(ref) + animations.skeleton.size());
			std::memcpy(out.data(), &ref, sizeof(ref));
			std::memcpy(out.data() + sizeof(ref), animations.skeleton.data(), ref.pathLength);
			return out;
		}

		void
		unpackSkeletonRef(AnimationSet& animations, std::span<const std::byte> bytes)
		{
			if (bytes.empty())
				return;

			if (bytes.size() < sizeof(SkeletonRef))
				throw std::runtime_error("banim: the skeleton reference chunk is truncated");

			SkeletonRef ref{};
			std::memcpy(&ref, bytes.data(), sizeof(ref));

			if (sizeof(ref) + ref.pathLength > bytes.size())
				throw std::runtime_error("banim: the skeleton path runs past its chunk");

			animations.skeletonSignature = ref.signature;
			animations.boneCount         = ref.boneCount;
			animations.skeleton.assign(
				reinterpret_cast<const char*>(bytes.data() + sizeof(ref)),
				ref.pathLength);
		}
	}

	std::vector<std::byte>
	serializeAnimations(const AnimationSet& animations)
	{
		chunk::Writer writer;
		writer.Add(ChunkId::kClips, animations.clips);
		writer.Add(ChunkId::kSamples, animations.samples);
		writer.Add(ChunkId::kStringPool, animations.stringPool);
		writer.Add(ChunkId::kSkeletonRef, packSkeletonRef(animations));
		return writer.Finish(c_Magic, c_VersionMajor, c_VersionMinor);
	}

	AnimationSet
	deserializeAnimations(std::span<const std::byte> bytes)
	{
		const chunk::Reader reader(bytes, c_Magic, c_VersionMajor, c_What);

		AnimationSet animations;
		animations.clips      = reader.Read<AnimationClip>(ChunkId::kClips);
		animations.samples    = reader.Read<Transform>(ChunkId::kSamples);
		animations.stringPool = reader.Read<char>(ChunkId::kStringPool);
		unpackSkeletonRef(animations, reader.Read<std::byte>(ChunkId::kSkeletonRef));

		validateAnimationSet(animations);
		return animations;
	}

	void
	saveAnimations(const AnimationSet& animations, const std::filesystem::path& path)
	{
		const auto bytes = serializeAnimations(animations);

		// Cleared so fileErrorMessage cannot blame a stale errno from an unrelated call for the failure.
		errno = 0;
		std::ofstream out(path, std::ios::binary);
		if (!out)
			throw std::runtime_error(fileErrorMessage("banim: cannot open file for writing", path));

		out.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!out)
			throw std::runtime_error(fileErrorMessage("banim: failed to write file", path));
	}

	AnimationSet
	loadAnimations(const std::filesystem::path& path)
	{
		return deserializeAnimations(core::file::readFileBytes(path.string()));
	}

	std::string
	loadAnimationSkeletonPath(const std::filesystem::path& path)
	{
		constexpr std::array<uint32_t, 1> c_Wanted = { { ChunkId::kSkeletonRef } };

		const auto chunks =
			chunk::readChunksFromFile(path, c_Magic, c_VersionMajor, c_Wanted, c_What);

		const auto it = chunks.find(ChunkId::kSkeletonRef);
		if (it == chunks.end())
			return {};

		AnimationSet animations;
		unpackSkeletonRef(animations, it->second);
		return animations.skeleton;
	}

	bool
	animationsMatchSkeleton(const AnimationSet& animations, const Skeleton& skeleton) noexcept
	{
		return animations.boneCount == skeleton.bones.size() &&
		       animations.skeletonSignature == skeletonSignature(skeleton);
	}
}
