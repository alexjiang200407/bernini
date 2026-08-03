#pragma once

namespace bgl
{
	enum class LayerType : uint8_t
	{
		kInvalid = static_cast<uint8_t>(-1),
		kOpaque  = 0,
		kMask,
		kBlend,

		// Stochastic coverage: alpha becomes a per-pixel hashed threshold rather than a cutoff, so
		// every layer of a self-occluding surface writes depth and participates. Resolves to the
		// correct blend only under temporal AA -- a single frame of it is noise.
		kHashed,
		kCount,
	};
}
