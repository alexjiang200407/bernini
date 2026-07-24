#include <assetlib/skeleton.h>

#include <assetlib/bmesh_io.h>

namespace assetlib
{
	namespace
	{
		// FNV-1a, so the signature is stable across runs, builds and platforms -- it is written into a
		// file and compared against one written by a different process.
		constexpr uint64_t c_FnvOffset = 14695981039346656037ull;
		constexpr uint64_t c_FnvPrime  = 1099511628211ull;

		void
		hashBytes(uint64_t& hash, const void* data, size_t size) noexcept
		{
			const auto* bytes = static_cast<const uint8_t*>(data);
			for (size_t i = 0; i < size; ++i)
			{
				hash ^= bytes[i];
				hash *= c_FnvPrime;
			}
		}
	}

	uint64_t
	skeletonSignature(const Skeleton& skeleton) noexcept
	{
		uint64_t hash = c_FnvOffset;
		for (const Bone& bone : skeleton.bones)
		{
			const std::string name = nameFromPool(skeleton.stringPool, bone.nameOffset);
			hashBytes(hash, name.data(), name.size());
			hashBytes(hash, &bone.parent, sizeof(bone.parent));
		}
		return hash;
	}

	void
	validateSkeleton(const Skeleton& skeleton)
	{
		for (size_t i = 0; i < skeleton.bones.size(); ++i)
		{
			const Bone& bone = skeleton.bones[i];

			if (bone.parent != c_InvalidIndex && bone.parent >= i)
				throw std::runtime_error(
					"skeleton: bone " + std::to_string(i) + " has parent " +
					std::to_string(bone.parent) +
					", which is not before it -- the bones are not topologically sorted");

			if (bone.nameOffset != 0 && bone.nameOffset >= skeleton.stringPool.size())
				throw std::runtime_error(
					"skeleton: bone " + std::to_string(i) +
					" names an offset past the string pool");
		}
	}

	void
	validateAnimationSet(const AnimationSet& animations)
	{
		if (animations.boneCount == 0)
		{
			if (!animations.samples.empty())
				throw std::runtime_error("animation: samples with no bones to address them by");
			return;
		}

		if (animations.samples.size() % animations.boneCount != 0)
			throw std::runtime_error("animation: the sample pool is not a whole number of poses");

		for (size_t i = 0; i < animations.clips.size(); ++i)
		{
			const AnimationClip& clip = animations.clips[i];

			if (clip.frameCount == 0)
				throw std::runtime_error("animation: clip " + std::to_string(i) + " has no frames");

			const size_t end = static_cast<size_t>(clip.firstSample) +
			                   static_cast<size_t>(clip.frameCount) * animations.boneCount;
			if (end > animations.samples.size())
				throw std::runtime_error(
					"animation: clip " + std::to_string(i) + " samples past the end of the pool");
		}
	}

	uint32_t
	findBone(const Skeleton& skeleton, std::string_view name)
	{
		for (size_t i = 0; i < skeleton.bones.size(); ++i)
			if (nameFromPool(skeleton.stringPool, skeleton.bones[i].nameOffset) == name)
				return static_cast<uint32_t>(i);

		return c_InvalidIndex;
	}

	std::vector<glm::mat4>
	bindPoseModelTransforms(const Skeleton& skeleton)
	{
		std::vector<glm::mat4> model(skeleton.bones.size());
		for (size_t i = 0; i < skeleton.bones.size(); ++i)
		{
			const Bone& bone  = skeleton.bones[i];
			const auto  local = toMatrix(bone.bindPose);
			model[i]          = bone.parent == c_InvalidIndex ? local : model[bone.parent] * local;
		}
		return model;
	}
}
