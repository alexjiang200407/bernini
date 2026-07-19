#pragma once
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "scene/ComputeBuffer.h"
#include "uniforms/Uniforms.h"

namespace bgl
{
	class FrameGraph;
	class IDevice;
	class IResourceManager;
	class PassContext;
	struct DrawData;

	/**
	 * Depth-sorts the transparent instances on the GPU.
	 *
	 * Blending needs back-to-front order, which cuts across the PSO bucketing the opaque path uses,
	 * so transparent instances are compacted into their own list and sorted by distance. The sort
	 * key also carries the occlude class, so the sorted list comes out split into
	 * [self-occluding][plain] -- the partition the forward pass draws as two indirect dispatches.
	 */
	class TransparentSortPass
	{
	public:
		TransparentSortPass() = default;
		~TransparentSortPass() noexcept { logger::trace("~TransparentSortPass"); }
		TransparentSortPass(IDevice* device, core::SharedRef<IResourceManager> resourceManager)
		{
			Init(device, resourceManager);
		}

		TransparentSortPass(const TransparentSortPass&) noexcept = delete;
		TransparentSortPass(TransparentSortPass&&) noexcept      = delete;

		TransparentSortPass&
		operator=(const TransparentSortPass&) noexcept = delete;

		TransparentSortPass&
		operator=(TransparentSortPass&&) noexcept = delete;

		void
		Init(IDevice* device, core::SharedRef<IResourceManager> resourceManager);

		void
		Release(uint64_t fenceVal, bool deferred = true);

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

		ComputeBuffer m_PartitionBase;
		ComputeBuffer m_PartitionDispatchArgs;
	};
}
