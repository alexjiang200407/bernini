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
	 * The mesh-emulation half of the geometry path: what the amplification stage does on a backend
	 * that has one. Counts the visible meshlets per PSO bucket, scans the counts into record
	 * regions, seeds each bucket's indirect draw args, and expands every visible instance into one
	 * MeshletInstance record per meshlet -- which the vertex-pulling forward draw then consumes via
	 * drawIndirect.
	 *
	 * Attached only when the device reports no mesh-shader support (IDevice::SupportsMeshShaders);
	 * the D3D12 path amplifies on the GPU and needs none of this.
	 */
	class ExpandMeshletsPass
	{
	public:
		ExpandMeshletsPass() = default;
		~ExpandMeshletsPass() noexcept { logger::trace("~ExpandMeshletsPass"); }

		ExpandMeshletsPass(const ExpandMeshletsPass&) noexcept = delete;
		ExpandMeshletsPass(ExpandMeshletsPass&&) noexcept      = delete;

		ExpandMeshletsPass&
		operator=(const ExpandMeshletsPass&) noexcept = delete;

		ExpandMeshletsPass&
		operator=(ExpandMeshletsPass&&) noexcept = delete;

		void
		Init(IDevice* device, core::SharedRef<IResourceManager> resourceManager);

		void
		Release(bool deferred = true);

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

	private:
		void
		ExecuteHistogramAndScan(const PassContext& ctx, const DrawData& draw);

		void
		ExecuteDrawArgs(const PassContext& ctx, const DrawData& draw);

		void
		ExecuteExpand(const PassContext& ctx, const DrawData& draw);

	private:
		ComputeKernel m_Histogram;
		ComputeKernel m_PrefixSum;
		ComputeKernel m_DrawArgs;
		ComputeKernel m_Expand;

		// One entry per PSO bucket, rewritten every draw; not tied to the scene.
		ComputeBuffer m_MeshletPrefixSum;
		ComputeBuffer m_DrawArgsBuffer;
	};
}
