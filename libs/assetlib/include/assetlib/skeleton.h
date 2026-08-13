#pragma once
#include <core/glm.h>

namespace assetlib
{
	struct AnimationSet;
	struct Skeleton;

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

	/** The index of the bone named `name`, or nullopt. Linear: only tooling asks. */
	[[nodiscard]] std::optional<uint32_t>
	findBone(const Skeleton& skeleton, std::string_view name);

	/**
	 * Each bone's model-space bind transform, in bone order. One forward pass, because the bones are
	 * topologically sorted.
	 */
	[[nodiscard]] std::vector<glm::mat4>
	bindPoseModelTransforms(const Skeleton& skeleton);

	/**
	 * Each bone's model-space transform at `frame` of `clip`, in bone order.
	 *
	 * The samples are local to each bone's parent, so this is the same forward pass
	 * bindPoseModelTransforms makes, over a clip's pose instead of the rest one.
	 *
	 * @throws std::runtime_error if `clip` or `frame` is out of range, if `animations` was cooked
	 *         against a different rig (by animationsMatchSkeleton), or if the clip's samples fall
	 *         outside the pool.
	 */
	[[nodiscard]] std::vector<glm::mat4>
	poseModelTransforms(
		const Skeleton&     skeleton,
		const AnimationSet& animations,
		uint32_t            clip,
		uint32_t            frame);

	/**
	 * The matrix each bone skins a vertex by: its model-space pose times its inverse bind.
	 *
	 * The inverse bind is what takes a vertex from model space into the bone's own space, so the
	 * product is identity for a pose that equals the bind pose -- which is why a rest-pose frame
	 * reproduces the source mesh exactly rather than approximately.
	 *
	 * @throws std::runtime_error if `modelTransforms` is not one per bone.
	 */
	[[nodiscard]] std::vector<glm::mat4>
	skinningMatrices(const Skeleton& skeleton, std::span<const glm::mat4> modelTransforms);
}
