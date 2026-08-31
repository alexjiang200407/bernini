#pragma once

namespace bgl
{
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
	 * What a skinned instance is spawned with, written once and never per frame. The pose at
	 * RenderJob::time `t` is frame `phase + t * rate * sampleRate`, wrapped over the clip when it
	 * loops and clamped to its last frame when it does not; fractional frames blend the two they
	 * fall between.
	 *
	 * `phase` staggers identical units for free, `rate` de-syncs their stride; `rate = 0` holds an
	 * instance on `phase` under any clock, which is also what a caller that never sets
	 * RenderJob::time gets.
	 *
	 * `source` is not one of the three playback fields -- it says where the pose is read from, not
	 * what plays, so moving a unit between the two sources rewrites none of them. It will not stay
	 * that way: the per-instance source is where a weighted clip list and a bone mask arrive, and
	 * neither has anywhere to live on a pose the whole rig shares.
	 */
	struct SkinnedInstanceDesc
	{
		uint32_t   clip   = 0;
		float      phase  = 0.0f;
		float      rate   = 1.0f;
		PoseSource source = PoseSource::kPerInstance;
	};
}
