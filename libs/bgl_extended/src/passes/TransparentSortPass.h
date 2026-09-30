#pragma once
#include "passes/PassInitContext.h"
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/uniforms/Uniforms.h>
#include <spdlog/spdlog.h>

namespace bgpu
{
	class PipelineBatch;
	class IDevice;
	class IResourceManager;
}

namespace bgl
{
	class FrameGraph;
	class PassContext;
	struct DrawData;

	/**
	 * Depth-sorts the transparent instances on the GPU.
	 *
	 * Blending needs back-to-front order, which cuts across the bucketing the opaque path uses,
	 * so transparent instances are compacted into their own list and sorted by distance. The forward
	 * pass draws that list whole, with one indirect dispatch whose count this pass emits.
	 */
	class TransparentSortPass
	{
	public:
		TransparentSortPass() = default;
		~TransparentSortPass() noexcept { spdlog::trace("~TransparentSortPass"); }

		TransparentSortPass(const TransparentSortPass&) noexcept = delete;
		TransparentSortPass(TransparentSortPass&&) noexcept      = delete;

		TransparentSortPass&
		operator=(const TransparentSortPass&) noexcept = delete;

		TransparentSortPass&
		operator=(TransparentSortPass&&) noexcept = delete;

		void
		Init(const PassInitContext& ctx);

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
		bgpu::ComputeKernel m_DepthKeys;
		bgpu::ComputeKernel m_Sort;
	};
}
