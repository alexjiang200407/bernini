#include "playback_writes.h"

#include "Windows/AnimationEditor/transition_spans.h"

#include <bgl/InstanceDesc.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gamelib/anim_blend.h>
#include <optional>

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

	float
	CutSeconds(const float sampleRate) noexcept
	{
		constexpr float c_Fallback = 1.0f / 60.0f;
		return std::isfinite(sampleRate) && sampleRate > 0.0f ? 1.0f / sampleRate : c_Fallback;
	}

	bgl::SkinnedPlaybackDesc
	TransitionPlayback(
		const uint32_t          fromNode,
		const uint32_t          toNode,
		const float             fromParameter,
		const float             toParameter,
		const TransitionLayout& layout)
	{
		// FromClip seeds a slot's phase and rate but has no parameter, so a space at the outgoing
		// end is written here; CrossfadeTo carries the incoming one.
		auto from           = bgl::SkinnedPlaybackDesc::FromClip(fromNode);
		from.slot[0].tRef   = layout.windowStart;
		from.slot[0].param0 = fromParameter;
		from.slot[0].param1 = fromParameter;

		return game::CrossfadeTo(
			from,
			toNode,
			layout.start,
			layout.duration,
			0.0f,
			1.0f,
			toParameter);
	}

	std::optional<int>
	SoloFromClip(const int fromNode, const int toNode) noexcept
	{
		if (fromNode < 0 || (toNode >= 0 && toNode != fromNode))
			return std::nullopt;
		return fromNode;
	}
}
