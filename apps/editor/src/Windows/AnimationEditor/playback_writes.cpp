#include "playback_writes.h"

#include <bgl/InstanceDesc.h>
#include <cstddef>
#include <cstdint>
#include <gamelib/anim_blend.h>

namespace editor
{
	bool
	RewritesPlayback(const bgl::PoseSource source) noexcept
	{
		return source == bgl::PoseSource::kPerInstance;
	}

	uint32_t
	DominantNode(const bgl::SkinnedPlaybackDesc& desc, const float nowSeconds) noexcept
	{
		size_t best       = 0;
		float  bestWeight = 0.0f;

		for (size_t s = 0; s < desc.slot.size(); ++s)
		{
			// Strictly greater, so a tie keeps the lowest slot.
			if (const float weight = game::SlotWeightAt(desc.slot[s], nowSeconds);
			    weight > bestWeight)
			{
				best       = s;
				bestWeight = weight;
			}
		}

		return desc.slot[best].nodeIndex;
	}
}
