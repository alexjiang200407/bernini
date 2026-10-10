#pragma once
#include <bgl/LodLevel.h>
#include <bgl/types/InstanceDesc.h>
#include <cstdint>
#include <optional>

namespace bgl
{
	/**
	 * How a view chooses each placement's level of detail. The default draws every mesh as it was
	 * authored: each level at the size its import document gave it, and a change of level dissolved
	 * over a few frames. ISceneView::SetLodSelection is what it refuses.
	 */
	struct LodSelectionDesc
	{
		// Every authored threshold, scaled: above 1 a mesh gives up detail at a larger size on
		// screen, below 1 it holds detail longer. A quality setting, or a bias for a slow machine.
		float pixelScale = 1.0f;

		// Draws this level of every placement, skipping the size test -- a mesh with fewer levels
		// draws its coarsest -- which is how a level is looked at on its own. Empty selects by size.
		std::optional<LodLevel> forceLevel;

		// Draws every placement past its last level -- its impostor, or nothing where its mesh has
		// none -- skipping the size test: how an impostor is looked at on its own. Refused with
		// forceLevel.
		bool forceImpostor = false;

		// How long a change of level dissolves over, the two levels dithered against each other
		// and resolved by temporal AA. 0 swaps in one frame. A PoseSource::kAuto placement changing
		// source dissolves over the same time.
		float fadeSeconds = 0.15f;

		// PoseSource::kAuto placements this view draws per instance at once; past it, the rest draw
		// from their rig's table however large they are. Zero draws every one from its table.
		uint32_t poseBudget = 256;

		// The size on screen, in pixels, below which a kAuto placement whose mesh has one level
		// draws from its table. A mesh with levels swaps where it leaves level 0 instead. Scaled by
		// pixelScale, as every threshold is.
		float posePixels = 160.0f;

		// Draws every kAuto placement from this source, skipping the size test -- per instance
		// still within poseBudget. Empty selects by size. kAuto is not a source to force.
		std::optional<PoseSource> forcePoseSource;
	};
}
