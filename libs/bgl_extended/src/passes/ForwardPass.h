#pragma once
#include "gfx/DrawBucketTable.h"
#include "passes/BlobShadowPhase.h"
#include "pipeline/MeshletKernel.h"
#include "types/DrawBucketMask.h"
#include "types/MeshletState.h"
#include <cstdint>
#include <span>
#include <spdlog/spdlog.h>
#include <vector>

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
			m_TransparentKernel.Reset();
			m_BlobShadows.Release();
		}

		/** Requests the always-on blob-shadow kernels; bucket kernels arrive by AddDrawBucketKernels. */
		void
		Init(IDevice* device, PipelineBatch& pipelines, const DrawBucketTable& buckets);

		/**
		 * Requests the kernels for the buckets set in `buckets` that are not already initialized;
		 * they are live once `pipelines` is built. A bucket already initialized is left alone.
		 * @pre every set bit is an allocated, non-transparent bucket.
		 */
		void
		AddDrawBucketKernels(
			IDevice*              device,
			PipelineBatch&        pipelines,
			const DrawBucketMask& demanded);

		/**
		 * Requests the one shared blend kernel the whole depth-sorted list draws through --
		 * demanded by any transparent bucket, owned by none.
		 */
		void
		AddTransparentKernel(IDevice* device, PipelineBatch& pipelines);

		[[nodiscard]] bool
		DrawBucketInitialized(uint32_t bucket) const noexcept
		{
			return bucket < m_Kernels.size() && m_Kernels[bucket].pipeline.IsInitialized();
		}

		[[nodiscard]] bool
		TransparentInitialized() const noexcept
		{
			return m_TransparentKernel.pipeline.IsInitialized();
		}

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

		/** The binder-name check over one kernel family; a family with nothing built is skipped. */
		void
		CheckKernelNames(std::span<const MeshletKernel> kernels) const;

		// Indexed by bucket id, grown to the table's count as buckets are demanded.
		std::vector<MeshletKernel> m_Kernels;

		// The shared blend kernel (see DrawTransparent); no bucket owns it.
		MeshletKernel m_TransparentKernel;

		const DrawBucketTable* m_DrawBucketTable = nullptr;

		// Drawn between the opaque buckets and DrawTransparent -- see BlobShadowPhase for why it
		// is a phase of this pass rather than a pass of its own.
		BlobShadowPhase m_BlobShadows;
	};
}
