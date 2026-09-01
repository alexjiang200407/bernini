#pragma once
#include "idl/PsoType.h"
#include "pipeline/MeshletKernel.h"
#include "types/MeshletState.h"

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
		 * The depth-sorted transparent phase: one indirect dispatch over the whole sorted list,
		 * back-to-front, drawn after the opaque buckets and inside the same pass so it shares the
		 * depth attachment.
		 *
		 * Binds its own framebuffers rather than reusing the opaque one: a blend PSO declares no
		 * velocity render target, and an attachment count that outruns the PSO's is invalid.
		 */
		void
		DrawTransparent(const DrawData& draw, const PassContext& resources);

		std::array<MeshletKernel, idl::c_PsoCount> m_Kernels;
	};
}
