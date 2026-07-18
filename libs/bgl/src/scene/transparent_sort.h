#pragma once

namespace bgl
{
	/**
	 * A contiguous span of the depth-sorted transparent list that shares one PSO. The forward pass
	 * draws one `DispatchMesh` per run, switching pipeline only where depth order forces it.
	 */
	struct TransparentRun
	{
		uint32_t pso;
		uint32_t offset;
		uint32_t count;
	};

	/**
	 * One transparent submesh instance awaiting sorting. `depth` is the sort key (larger is farther
	 * from the camera); `instanceIndex` is the dense index into the view's instance buffer.
	 */
	struct TransparentDrawable
	{
		float    depth;
		uint32_t pso;
		uint32_t instanceIndex;
	};

	/**
	 * Orders `drawables` back-to-front (farthest first) and splits the result into maximal runs of
	 * equal PSO. Writes the ordered instance indices to `outOrder` and the runs to `outRuns` (both
	 * cleared first).
	 *
	 * Ties on `depth` break by `instanceIndex`, so a given set of drawables yields the same order
	 * regardless of the order they were supplied in -- the invariant the render path relies on for a
	 * stable, camera-only result.
	 */
	void
	BuildTransparentDrawOrder(
		std::span<TransparentDrawable> drawables,
		std::vector<uint32_t>&         outOrder,
		std::vector<TransparentRun>&   outRuns);
}
