#pragma once
#include "pipeline/MeshletKernel.h"
#include "types/MeshletState.h"
#include <bgl/PsoType.h>

namespace bgl
{
	class IDevice;
	class FrameGraph;
	class PassContext;

	struct DrawData;

	class ForwardPass
	{
	public:
		ForwardPass() = default;
		~ForwardPass() noexcept { logger::trace("~ForwardPass"); }
		ForwardPass(IDevice* device) { Init(device); }

		ForwardPass(const ForwardPass&) noexcept = delete;
		ForwardPass(ForwardPass&&) noexcept      = delete;

		ForwardPass&
		operator=(const ForwardPass&) noexcept = delete;

		ForwardPass&
		operator=(ForwardPass&&) noexcept = delete;

		void
		Release()
		{
			for (MeshletKernel& kernel : m_Kernels)
			{
				kernel.Reset();
			}
			for (MeshletKernel& kernel : m_PrepassKernels)
			{
				kernel.Reset();
			}
		}

		void
		Init(IDevice* device);

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

		void
		Execute(const DrawData& draw, const PassContext& resources);

	private:
		/** Binds the geometry, material, and IBL uniforms common to every forward draw. */
		void
		BindKernel(MeshletKernel& kernel, const DrawData& draw, const PassContext& resources);

		/**
		 * The depth-sorted transparent phase, drawn after the opaque buckets and inside the same pass:
		 * a depth-only pre-pass over the self-occluding partition, then both partitions' blended
		 * draws back-to-front. The pre-pass has to share this pass's depth attachment and sit between
		 * the colour draws, which is why it is a sub-draw here rather than a pass of its own.
		 */
		void
		DrawTransparent(
			const DrawData&    draw,
			const PassContext& resources,
			MeshletState       colorState);

		std::array<MeshletKernel, c_PsoCount> m_Kernels;

		// Depth-only pre-pass kernels; only the self-occluding transparent PSO slots are built.
		std::array<MeshletKernel, c_PsoCount> m_PrepassKernels;
	};
}
