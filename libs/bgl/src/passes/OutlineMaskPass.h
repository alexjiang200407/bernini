#pragma once
#include "pipeline/MeshletKernel.h"

namespace bgl
{
	class IDevice;
	class FrameGraph;
	class PassContext;
	struct DrawData;

	/**
	 * Draws a view's selected submesh instances into the target's R8 outline mask, which the
	 * post-process dilates into the outline.
	 *
	 * The same amplification/mesh shaders as the forward pass, dispatched directly over the
	 * view's CPU-built selected list -- no culling, no indirect args. The draw is depth-free (the
	 * outline shows the full silhouette through occluders) and uses the unjittered
	 * view-projection: an overlay nothing accumulates must not shimmer with TAA.
	 */
	class OutlineMaskPass
	{
	public:
		OutlineMaskPass() = default;
		~OutlineMaskPass() noexcept { logger::trace("~OutlineMaskPass"); }

		OutlineMaskPass(const OutlineMaskPass&) noexcept = delete;
		OutlineMaskPass(OutlineMaskPass&&) noexcept      = delete;

		OutlineMaskPass&
		operator=(const OutlineMaskPass&) noexcept = delete;

		OutlineMaskPass&
		operator=(OutlineMaskPass&&) noexcept = delete;

		void
		Init(IDevice* device);

		void
		Release()
		{
			m_Kernel.Reset();
		}

		/** @pre `selectedCount` > 0 -- the caller skips the pass for an empty selection. */
		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw, uint32_t selectedCount);

	private:
		void
		Execute(const DrawData& draw, uint32_t selectedCount, const PassContext& resources);

		MeshletKernel m_Kernel;
	};
}
