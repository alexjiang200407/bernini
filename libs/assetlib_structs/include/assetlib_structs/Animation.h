#pragma once
#include <assetlib_structs/Node.h>

namespace assetlib
{
	/** The rate clips are resampled to unless a caller asks for another. */
	inline constexpr float c_DefaultSampleRate = 30.0f;

	/**
	 * One clip, resampled to a uniform rate. A runtime addresses a pose by index rather than
	 * searching keyframes, which is what makes evaluating thousands of units per frame affordable.
	 *
	 * Frames span the closed interval [0, duration]: frame 0 is the pose at t = 0 and frame
	 * `frameCount - 1` the pose at t = `duration`. A clip that loops therefore ends on a duplicate
	 * of its own first pose, and a runtime that plays both stutters by one frame.
	 */
	struct AnimationClip
	{
		uint32_t nameOffset;   // into AnimationSet::stringPool
		uint32_t firstSample;  // into AnimationSet::samples
		uint32_t frameCount;   // >= 1
		float    sampleRate;   // Hz
		float    duration;     // seconds; 0 for a single-frame pose

		/** Bone 0's translation across the clip. Cosmetic: it never drives a unit's position. */
		glm::vec3 rootMotion;

		/** Horizontal `rootMotion` over `duration`, in units per second. 0 for an in-place clip. */
		float locomotionSpeed;

		uint32_t loop;  // 1 when the last pose matches the first
	};

	static_assert(sizeof(AnimationClip) == 40);

	/**
	 * The clips authored against one skeleton, and the poses they sample.
	 *
	 * `samples` is frame-major -- bone `b` of frame `f` of a clip is
	 * `samples[clip.firstSample + f * boneCount + b]` -- because a pose is always evaluated whole
	 * and a bone across time never.
	 */
	struct AnimationSet
	{
		std::string skeleton;  // `.bskel` path, relative to the data root
		uint64_t    skeletonSignature = 0;
		uint32_t    boneCount         = 0;

		std::vector<AnimationClip> clips;
		std::vector<Transform>     samples;
		std::vector<char> stringPool;  // NUL-terminated names; offset 0 is the empty string
	};
}
