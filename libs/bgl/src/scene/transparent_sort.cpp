#include "scene/transparent_sort.h"

namespace bgl
{
	void
	BuildTransparentDrawOrder(
		std::span<TransparentDrawable> drawables,
		std::vector<uint32_t>&         outOrder,
		std::vector<TransparentRun>&   outRuns)
	{
		outOrder.clear();
		outRuns.clear();

		std::sort(
			drawables.begin(),
			drawables.end(),
			[](const TransparentDrawable& a, const TransparentDrawable& b) {
				if (a.depth != b.depth)
				{
					return a.depth > b.depth;
				}
				return a.instanceIndex < b.instanceIndex;
			});

		outOrder.reserve(drawables.size());

		for (const TransparentDrawable& drawable : drawables)
		{
			if (outRuns.empty() || outRuns.back().pso != drawable.pso)
			{
				outRuns.push_back(
					TransparentRun{ drawable.pso, static_cast<uint32_t>(outOrder.size()), 0u });
			}

			++outRuns.back().count;
			outOrder.push_back(drawable.instanceIndex);
		}
	}
}
