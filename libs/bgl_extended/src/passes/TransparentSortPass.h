#pragma once
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "uniforms/Uniforms.h"

namespace bgl
{
	class PipelineBatch;

	class FrameGraph;
	class IDevice;
	class IResourceManager;
	class PassContext;
	struct DrawData;

	/**
	 * Depth-sorts the transparent instances on the GPU.
	 *
	 * Blending needs back-to-front order, which cuts across the PSO bucketing the opaque path uses,
	 * so transparent instances are compacted into their own list and sorted by distance. The forward
	 * pass draws that list whole, with one indirect dispatch whose count this pass emits.
	 */
	class TransparentSortPass
	{
	public:
		TransparentSortPass() = default;
		~TransparentSortPass() noexcept { logger::trace("~TransparentSortPass"); }

		TransparentSortPass(const TransparentSortPass&) noexcept = delete;
		TransparentSortPass(TransparentSortPass&&) noexcept      = delete;

		TransparentSortPass&
		operator=(const TransparentSortPass&) noexcept = delete;

		TransparentSortPass&
		operator=(TransparentSortPass&&) noexcept = delete;

		void
		Init(IDevice* device, PipelineBatch& pipelines);

		// Owns no GPU storage -- the sort buffers live on the view's TransparentSortState, one set
		// per view rather than per frustum -- so this only drops the kernels.
		void
		Release();

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

	private:
		void
		ExecuteClear(const PassContext& ctx);

		void
		ExecuteDepthKeys(const PassContext& ctx, const DrawData& draw);

		void
		ExecuteSort(const PassContext& ctx, const DrawData& draw);

	private:
		ComputeKernel m_DepthKeys;
		ComputeKernel m_Sort;
	};
}
