#pragma once

#include <bgl/InstanceDesc.h>
#include <cstdint>

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
	 */
	[[nodiscard]] uint32_t
	DominantNode(const bgl::SkinnedPlaybackDesc& desc, float nowSeconds) noexcept;
}
