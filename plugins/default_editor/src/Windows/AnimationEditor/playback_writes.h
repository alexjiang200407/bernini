#pragma once

#include "Windows/AnimationEditor/transition_spans.h"

#include <bgl/InstanceDesc.h>
#include <cstdint>
#include <optional>

namespace editor
{
	/**
	 * Whether instances on `source` take a playback rewrite at all.
	 *
	 * The per-instance source owns a palette and a record of weighted slots, so
	 * `ISceneView::SetSkinnedPlayback` rewrites it in place. The crowd source reads the rig's
	 * shared bone-anim table, which holds one clip and no slots and which no instance may write --
	 * the rewrite throws there, and a change of clip is a respawn.
	 */
	[[nodiscard]] bool
	RewritesPlayback(bgl::PoseSource source) noexcept;

	/**
	 * The node `desc` is mostly showing at `nowSeconds` -- what a fresh single-clip spawn takes so
	 * a respawn lands on the pose the record was already displaying.
	 *
	 * `SkinnedInstanceDesc` carries one clip, so a respawn cannot carry a record across; this is
	 * the one slot of it that survives. Ties go to the lowest slot, and a record where no slot
	 * carries weight yet answers slot 0, which is what such a record shows.
	 *
	 * A slot's weight is the whole pose's, which is what makes one node *the* answer. A bone mask
	 * (`ROADMAP.md` § Skinned Meshes, unticked) would weight per bone, and no single node would be
	 * dominant -- an upper body on one clip over a lower body on another has no one clip to respawn
	 * onto. This returns the wrong answer that day rather than a wrong-looking one, so it is the
	 * caller that has to change.
	 */
	[[nodiscard]] uint32_t
	DominantNode(const bgl::SkinnedPlaybackDesc& desc, float nowSeconds) noexcept;

	/**
	 * The shortest fade that is still a fade, for a clip sampled at `sampleRate` -- what stands in
	 * for a cut when blending is switched off.
	 *
	 * Deliberately not zero. A fade of no duration does not cut: every ramp it writes has already
	 * completed at the moment it is written, so every slot reads zero weight there, the eviction
	 * search overwrites the outgoing slot, and the record then shows the *destination* at every
	 * earlier clock -- the whole window, rather than the half after the cut. One sample interval is
	 * the shortest window that still leaves the outgoing clip in the record saying what it played.
	 *
	 * A non-positive or non-finite rate falls back to a sixtieth, since a cut has to be drawable
	 * whatever the clip claims.
	 */
	[[nodiscard]] float
	CutSeconds(float sampleRate) noexcept;

	/**
	 * The record that previews a fade from `fromNode` onto `toNode` across `layout`: the outgoing end
	 * alone from the window's start, then `game::CrossfadeTo` the incoming one at `layout.start`
	 * over `layout.duration`. `fromParameter` and `toParameter` are where a blend-space end sits.
	 *
	 * The outgoing end plays from its first frame at `layout.windowStart`, since the preview clock is
	 * absolute and a one-shot anchored anywhere earlier has already clamped to its last frame.
	 *
	 * @throws std::runtime_error as `game::CrossfadeTo` does, on a negative or non-finite duration.
	 */
	[[nodiscard]] bgl::SkinnedPlaybackDesc
	TransitionPlayback(
		uint32_t                fromNode,
		uint32_t                toNode,
		float                   fromParameter,
		float                   toParameter,
		const TransitionLayout& layout);

	/**
	 * The clip the Blend tab plays alone for its ends `fromNode` and `toNode`, each -1 when unset:
	 * From, whenever the two do not name a fade -- To unset, or naming From itself -- and nothing
	 * when From is unset or the pair is a fade.
	 */
	[[nodiscard]] std::optional<int>
	SoloFromClip(int fromNode, int toNode) noexcept;
}
