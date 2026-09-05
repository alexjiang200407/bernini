#pragma once
#include <assetlib_structs/Node.h>
#include <core/str/string_pool.h>

#include <assetlib_structs/SourceRef.h>
#include <cstdint>
#include <string>
#include <vector>

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

		/** How far the cook moved this clip down to rest it on the floor; see assetlib::groundClips. */
		float groundOffset;
	};

	static_assert(sizeof(AnimationClip) == 44);

	/** A floor authored for one clip, overruling what the cook measures; see assetlib::groundClips. */
	struct ClipFloor
	{
		std::string clip;   // the clip's name, as its source gave it
		float       floor;  // the height to rest on y = 0

		bool
		operator==(const ClipFloor&) const = default;
	};

	/**
	 * The box one mesh entry sweeps through every pose of every clip, measured at cook so a load
	 * culls by it without re-skinning the rig -- see assetlib::findPosedBounds.
	 *
	 * `sourceSignature` names the geometry and bind the box was measured against
	 * (assetlib::posedBoundsSignature); a pairing whose signature differs is measured instead.
	 */
	struct PosedBox
	{
		uint64_t  sourceSignature;
		glm::vec3 min;
		glm::vec3 max;
		uint32_t  meshIndex;
	};

	static_assert(sizeof(PosedBox) == 40);

	/**
	 * How planted each leg is in each frame of the sample pool, measured at cook so a load plants a
	 * foot without walking the clips -- see assetlib::findPlantWeights.
	 *
	 * One byte per leg per frame, frame-major over the whole pool exactly as `samples` is: leg `l`
	 * of global frame `f` is `weights[f * legCount + l]`, so a clip reaches its own frames through
	 * `AnimationClip::firstSample`. A weight and not a flag, because a foot that planted in one
	 * frame would pop: the cook ramps each planted run in and out, and the shader reads the ramp
	 * rather than reconstructing it.
	 *
	 * `signature` names the legs, the rig and the geometry the measurement was made against
	 * (assetlib::plantWeightsSignature); a pairing whose signature differs is measured at load
	 * instead, the way a posed box that does not match is.
	 */
	struct PlantWeights
	{
		uint64_t             signature = 0;
		uint32_t             legCount  = 0;
		std::vector<uint8_t> weights;

		/** Empty is the ordinary case: most rigs author no avatar, so most clip sets carry none. */
		[[nodiscard]] bool
		Empty() const noexcept
		{
			return legCount == 0 || weights.empty();
		}

		bool
		operator==(const PlantWeights&) const = default;
	};

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
		core::string_pool          stringPool;

		std::vector<PosedBox> posedBoxes;  // empty until a cook bakes them

		// Empty until a cook bakes them, and empty forever for a rig that authors no avatar.
		PlantWeights plantWeights;

		SourceRef source;  // the copied .glb this was derived from; empty key when never recorded
	};
}
