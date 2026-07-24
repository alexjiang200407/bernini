#pragma once
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Skeleton.h>

namespace assetlib
{
	/**
	 * A hash over every bone's name and parent -- everything a joint index means.
	 *
	 * A clip set and a skinned mesh both address a skeleton by bare index, and nothing about a wrong
	 * index is detectable from the pose it produces. So each records the signature of the rig it was
	 * built against, and a re-export that inserts or reorders a bone is caught when the two are
	 * brought together rather than by a limb pointing the wrong way.
	 *
	 * Deliberately not a hash of the bind pose: re-authoring the rest pose of a rig does not
	 * invalidate a clip, and treating it as though it did would make every skeleton edit a re-cook of
	 * every clip set.
	 */
	[[nodiscard]] uint64_t
	skeletonSignature(const Skeleton& skeleton) noexcept;

	/**
	 * @throws std::runtime_error if the bones are not topologically sorted (a parent at or after its
	 *         child), a parent index is out of range, or a name offset is past the string pool.
	 */
	void
	validateSkeleton(const Skeleton& skeleton);

	/**
	 * @throws std::runtime_error if a clip's sample range falls outside `samples`, its frame count is
	 *         zero, or `boneCount` disagrees with the sample pool.
	 */
	void
	validateAnimationSet(const AnimationSet& animations);

	/** The index of the bone named `name`, or c_InvalidIndex. Linear: only tooling asks. */
	[[nodiscard]] uint32_t
	findBone(const Skeleton& skeleton, std::string_view name);

	/**
	 * Each bone's model-space bind transform, in bone order. One forward pass, because the bones are
	 * topologically sorted.
	 */
	[[nodiscard]] std::vector<glm::mat4>
	bindPoseModelTransforms(const Skeleton& skeleton);
}
