#include <assetlib/skeleton.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Skeleton.h>

#include <assetlib/bmesh_io.h>

#include <core/err/util.h>
#include <core/hash.h>

namespace assetlib
{
	using core::throw_runtime_error;

	// Persisted in `.banim`, so a change to core's hash invalidates every file already written and
	// needs a major version bump with it.
	uint64_t
	skeletonSignature(const Skeleton& skeleton) noexcept
	{
		uint64_t hash = core::hash_seed();
		for (const Bone& bone : skeleton.bones)
		{
			hash = core::hash_string(skeleton.stringPool.at(bone.nameOffset), hash);
			hash = core::hash_pod(bone.parent, hash);
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
				throw_runtime_error(
					"skeleton: bone {} has parent {}, which is not before it -- the bones are not "
					"topologically sorted",
					i,
					bone.parent);

			if (bone.nameOffset != 0 && skeleton.stringPool.at(bone.nameOffset).empty())
				throw_runtime_error("skeleton: bone {} names nothing in the string pool", i);
		}
	}

	void
	validateAnimationSet(const AnimationSet& animations)
	{
		if (animations.boneCount == 0)
		{
			if (!animations.samples.empty())
				throw_runtime_error("animation: samples with no bones to address them by");
			return;
		}

		if (animations.samples.size() % animations.boneCount != 0)
			throw_runtime_error("animation: the sample pool is not a whole number of poses");

		for (size_t i = 0; i < animations.clips.size(); ++i)
		{
			const AnimationClip& clip = animations.clips[i];

			if (clip.frameCount == 0)
				throw_runtime_error("animation: clip {} has no frames", i);

			const size_t end = static_cast<size_t>(clip.firstSample) +
			                   static_cast<size_t>(clip.frameCount) * animations.boneCount;
			if (end > animations.samples.size())
				throw_runtime_error("animation: clip {} samples past the end of the pool", i);
		}
	}

	uint32_t
	findBone(const Skeleton& skeleton, std::string_view name)
	{
		for (size_t i = 0; i < skeleton.bones.size(); ++i)
			if (skeleton.stringPool.at(skeleton.bones[i].nameOffset) == name)
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
