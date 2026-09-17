#pragma once
#include "passes/BlobShadowPhase.h"
#include "pipeline/MeshletKernel.h"
#include "types/MeshletState.h"
#include "types/PsoRowMask.h"
#include "types/RasterState.h"
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
			m_BlobShadows.Release();
		}

		/** Requests the always-on blob-shadow kernels; row kernels arrive by AddRowKernels. */
		void
		Init(IDevice* device, PipelineBatch& pipelines);

		/**
		 * Requests the kernels for the rows set in `rows` that are not already built; they are
		 * live once `pipelines` is built. A row already built is left alone.
		 */
		void
		AddRowKernels(IDevice* device, PipelineBatch& pipelines, const PsoRowMask& rows);

		/** @pre pso < idl::c_PsoCount. */
		[[nodiscard]] bool
		RowBuilt(uint16_t pso) const noexcept
		{
			return m_Kernels[pso].pipeline.IsInitialized();
		}

		/** @pre the batch Init requested into has been built. Fatal on a binder name no PSO declares. */
		void
		CheckBindings() const;

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

		void
		Execute(const DrawData& draw, const PassContext& resources);

		/**
		 * How `pso`'s pipeline culls in hardware. A row that culls nothing leaves back faces to the
		 * mesh stage, which reads each material's doubleSided flag. A pass that draws the same buckets
		 * must mirror this, or its depth holds faces the colour pass never drew.
		 *
		 * @pre pso < idl::c_PsoCount.
		 */
		[[nodiscard]] static RasterCullMode
		PsoCullMode(uint16_t pso) noexcept;

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

		// Drawn between the opaque buckets and DrawTransparent -- see BlobShadowPhase for why it
		// is a phase of this pass rather than a pass of its own.
		BlobShadowPhase m_BlobShadows;
	};
}
