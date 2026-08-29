#pragma once

namespace bgl
{
	/**
	 * The playback record a VAT instance is spawned with, written once and never per frame -- the
	 * tier's whole bargain. The pose at RenderJob::time `t` is frame `phase + t * rate * sampleRate`,
	 * wrapped over the clip when it loops and clamped to its last frame when it does not; fractional
	 * frames blend the two rows they fall between.
	 *
	 * `phase` staggers identical units for free, `rate` de-syncs their stride; `rate = 0` holds an
	 * instance on `phase` under any clock, which is also what a caller that never sets
	 * RenderJob::time gets.
	 */
	struct VatInstanceDesc
	{
		uint32_t clip  = 0;
		float    phase = 0.0f;  // frames, may be fractional
		float    rate  = 1.0f;  // multiplier on the clip's authored sampleRate
	};

	/** Where a skinned instance's pose comes from. See docs/skinning.md. */
	enum class PoseSource : uint32_t
	{
		/**
		 * SkinnedPosePass computes this instance's pose every frame, into a palette slice of its
		 * own. The hero tier: a pose no other instance shares, and the only source a per-unit blend,
		 * mask or IK can ever vary.
		 */
		kPerInstance,

		/**
		 * The mesh shader reads the rig's bone anim table, posed once and shared by every instance
		 * on that rig. The crowd tier: no per-unit work in any frame, at the cost of a pose that
		 * cannot vary per unit beyond `{clip, phase, rate}`.
		 *
		 * @pre the geom's rig is one AddRig returned; creating the instance is what asks for the
		 *      table, and the first frame after fills it.
		 */
		kBoneAnimTable,
	};

	/**
	 * The skinned tier's counterpart to VatInstanceDesc, and deliberately the same three playback
	 * fields with the same meanings: both derive their pose from RenderJob::time alone, so a unit can
	 * be moved between tiers without its playback record being rewritten.
	 *
	 * `source` is not one of those three. It says where the pose is read from, not what plays, so a
	 * unit that moves between the hero and crowd tiers keeps its `{clip, phase, rate}` and changes
	 * only this.
	 *
	 * A distinct type rather than an alias: the two are interchangeable today and will not stay so --
	 * the skinned tier is where a weighted clip list and a bone mask arrive.
	 */
	struct SkinnedInstanceDesc
	{
		uint32_t   clip   = 0;
		float      phase  = 0.0f;
		float      rate   = 1.0f;
		PoseSource source = PoseSource::kPerInstance;
	};
}
