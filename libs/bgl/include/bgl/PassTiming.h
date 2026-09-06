#pragma once
#include <string>

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
}
