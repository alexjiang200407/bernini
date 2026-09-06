#pragma once
#include "pipeline/MeshletKernel.h"
#include "types/MeshletState.h"
#include <array>
#include <bgl_common/idl/PsoType.h>
#include <spdlog/spdlog.h>

namespace bgl
{
	class PipelineBatch;

	class IDevice;
	class FrameGraph;
	class PassContext;

	struct DrawData;

	class ForwardPass
	{
	public:
		ForwardPass() = default;
		~ForwardPass() noexcept { logger::trace("~ForwardPass"); }

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

		/** Requests every PsoType's kernel; they are live once `pipelines` is built. */
		void
		Init(IDevice* device, PipelineBatch& pipelines);

		/** @pre the batch Init requested into has been built. Fatal on a binder name no PSO declares. */
		void
		CheckBindings() const;

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
