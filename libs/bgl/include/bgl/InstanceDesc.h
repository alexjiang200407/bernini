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
	 * what plays, so moving a unit between the two sources rewrites none of them. One clip is the
	 * whole of what a shared pose can hold; a per-instance one holds the weighted slots of
	 * SkinnedPlaybackDesc, which this is the one-slot spelling of.
	 */
	struct SkinnedInstanceDesc
	{
		uint32_t   clip   = 0;
		float      phase  = 0.0f;
		float      rate   = 1.0f;
		PoseSource source = PoseSource::kPerInstance;
	};

	/** Weighted slots a per-instance playback record holds. */
	inline constexpr uint32_t c_BlendSlots = 4;

	/**
	 * One weighted slot of a per-instance playback record: a node of the geom's rig, where its
	 * playback sat at `tRef`, and the ramps that move its weight and its parameter over time. A
	 * node is a clip index for now; an authored blend space is what will widen the table.
	 *
	 * Time is the only per-frame input, so a slot holds what to evaluate rather than a value: at
	 * clock `t` the weight is `weight0` before `rampStart`, `weight1` from `rampEnd` and linear
	 * between (an end at or before its start is a step), and the frame is `phase` advanced by
	 * `(t - tRef) * rate * sampleRate`, wrapped or clamped as the clip says. The live weights are
	 * normalized across the record's slots where they are read, so a set need not sum to anything.
	 *
	 * The parameter ramp has the same shape and is read only by a blend-space node; a clip node
	 * ignores it.
	 */
	struct PlaybackSlot
	{
		uint32_t node  = 0;
		float    phase = 0.0f;
		float    rate  = 1.0f;
		float    tRef  = 0.0f;

		float weight0   = 0.0f;
		float weight1   = 0.0f;
		float rampStart = 0.0f;
		float rampEnd   = 0.0f;

		float param0     = 0.0f;
		float param1     = 0.0f;
		float paramStart = 0.0f;
		float paramEnd   = 0.0f;
	};

	/**
	 * The whole playback record of a per-instance skinned placement: the slots blended into the
	 * instance's pose, evaluated from the clock alone. Every slot is read and every slot names a
	 * node of the rig, an unused one at weight zero. At a clock where no slot carries weight --
	 * every ramp still ahead of it -- slot 0 plays alone at full weight, so a record's slot 0 is
	 * what it shows until its ramps begin.
	 *
	 * Written at spawn or by ISceneView::SetSkinnedPlayback, never per frame. A rewrite must leave
	 * the pose at the previous frame's time what the old record gave it -- rebase every slot to
	 * now, start every ramp at or after now, ramp down rather than drop a slot still weighted -- or
	 * the motion vector that frame reprojects through a pose nothing drew.
	 */
	struct SkinnedPlaybackDesc
	{
		// Declared so this is not an aggregate: a braced `{clip, phase, rate}` must resolve to
		// SkinnedInstanceDesc alone rather than elide its way into the first slot of this one.
		SkinnedPlaybackDesc() noexcept = default;

		// `slot`, not `slots`: the latter is a Qt keyword macro in any client compiled with Qt's
		// keywords on, and a public header cannot know which of its clients those are.
		std::array<PlaybackSlot, c_BlendSlots> slot = {};

		/** Slot 0 playing `clip` at full weight from `phase` at `rate`, the rest weightless. */
		[[nodiscard]] static SkinnedPlaybackDesc
		FromClip(uint32_t clip, float phase = 0.0f, float rate = 1.0f) noexcept
		{
			auto desc            = SkinnedPlaybackDesc();
			desc.slot[0].node    = clip;
			desc.slot[0].phase   = phase;
			desc.slot[0].rate    = rate;
			desc.slot[0].weight0 = 1.0f;
			desc.slot[0].weight1 = 1.0f;
			return desc;
		}
	};
}
