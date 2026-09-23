#pragma once
#include "gfx/DrawBucketTable.h"
#include "passes/PassInitContext.h"
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

	/**
	 * Which half of the forward render a pass records. The static tier draws first, so that between
	 * the two the depth holds static receivers alone: the blob-shadow pass draws there, and it is
	 * where an HZB build belongs.
	 */
	enum class ForwardPhase : uint8_t
	{
		kStatic,
		kUnits,
	};

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
		}

		void
		Init(const PassInitContext& ctx);

		/**
		 * Requests the kernels for the buckets set in `buckets` that are not already initialized;
		 * they are live once `pipelines` is built. A bucket already initialized is left alone.
		 * @pre every set bit is an allocated, non-transparent bucket.
		 */
		void
		AddDrawBucketKernels(const PassInitContext& ctx, const DrawBucketMask& demanded);

		/**
		 * Requests the one shared blend kernel the whole depth-sorted list draws through --
		 * demanded by any transparent bucket, owned by none.
		 */
		void
		AddTransparentKernel(const PassInitContext& ctx);

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

		/**
		 * `kStatic` draws the non-transparent static buckets; `kUnits` the non-transparent buckets
		 * of every other tier, then the depth-sorted transparent list.
		 */
		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw, ForwardPhase phase);

	private:
		void
		Execute(const DrawData& draw, const PassContext& resources, ForwardPhase phase);

		/** Binds the geometry, material, and IBL uniforms common to every forward draw. */
		void
		BindKernel(MeshletKernel& kernel, const DrawData& draw, const PassContext& resources);

		/**
		 * The depth-sorted transparent phase: one indirect dispatch over the whole sorted list,
		 * back-to-front, drawn after the unit buckets and inside the same pass.
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
	};
}
