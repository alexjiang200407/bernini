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
		/** A palette of its own, posed every frame -- the only source a per-unit blend can vary. */
		kPerInstance,

		/**
		 * The rig's bone anim table, posed once and shared by every instance on it. Creating the
		 * instance is what asks for the table; the first frame after fills it.
		 */
		kBoneAnimTable,
	};

	/**
	 * The skinned tier's counterpart to VatInstanceDesc, and deliberately the same three playback
	 * fields with the same meanings: both derive their pose from RenderJob::time alone, so a unit can
	 * be moved between tiers without its playback record being rewritten. `source` is not one of the
	 * three -- it says where the pose is read from, not what plays.
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
