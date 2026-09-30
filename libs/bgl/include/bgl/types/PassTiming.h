#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bgl
{
	/** What one frame graph pass cost on the GPU, as IGraphics::GetPassTimings reports it. */
	struct PassTiming
	{
		std::string name;

		// Zero for a pass the GPU could not sample -- one that recorded nothing it can attach a
		// timestamp to -- rather than absent, so the rows still list every pass the frame ran.
		double milliseconds = 0.0;
	};

	/** One frame's pass costs, and which frame they came from. */
	struct PassTimings
	{
		// Increases when a newer frame's samples resolve, repeats while none have, and is zero
		// before the first timed frame. Opaque: a caller sampling every frame compares it against
		// the last id it saw to tell a new frame from the same one read twice, and counts nothing
		// with it -- frames whose samples never resolved leave no gap in it.
		uint64_t frame = 0;

		std::vector<PassTiming> passes;
	};
}
