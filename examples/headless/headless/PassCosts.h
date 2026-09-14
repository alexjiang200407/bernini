#pragma once
#include <bgl/PassHistory.h>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace headless
{
	/** What one pass cost over a run: the middle and the worst of the frames it ran in. */
	struct PassCost
	{
		std::string name;
		double      median = 0.0;
		double      max    = 0.0;
		std::size_t frames = 0;
	};

	/** Every pass's cost, costliest first, and the frame total's -- absent for an empty history. */
	struct PassCosts
	{
		std::vector<PassCost>   passes;
		std::optional<PassCost> frame;
	};

	/**
	 * Median and max per pass over `history`. The median rather than the mean, because one frame
	 * that stalled moves a mean and the question is what a frame costs rather than what the run
	 * took. A frame the graph culled a pass from is not a zero-cost sample for it: it is left out.
	 */
	[[nodiscard]] PassCosts
	SummarisePasses(const bgl::PassHistory& history);

	/** The costs as a fixed-width table, the frame total ruled off below the passes. */
	void
	PrintPassCosts(std::ostream& out, const PassCosts& costs);
}
