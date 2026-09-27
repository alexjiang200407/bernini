#pragma once
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
		std::optional<uint32_t> forceLevel;

		// How long a change of level dissolves over, the two levels dithered against each other
		// and resolved by temporal AA. 0 swaps in one frame.
		float fadeSeconds = 0.15f;
	};
}
