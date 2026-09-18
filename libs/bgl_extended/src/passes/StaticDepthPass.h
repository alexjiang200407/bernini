#pragma once
#include "gfx/DrawBucketTable.h"
#include "pipeline/MeshletKernel.h"
#include "types/DrawBucketMask.h"
#include <spdlog/spdlog.h>
#include <vector>

namespace bgl
{
	class PipelineBatch;

	class FrameGraph;
	class IDevice;
	class PassContext;

	struct DrawData;

	/**
	 * Renders the static geometry's depth into the target's receiver texture, ahead of the Forward
	 * pass: what the blob-shadow decal reconstructs the surface under each pixel from.
	 *
	 * Statics only, so a shadow never lands on another unit passing beneath its caster; the decal
	 * still occludes against the full scene depth. Statics are therefore drawn twice per frame --
	 * accepted, and repaid when the HZB milestone promotes this into the shared depth prepass the
	 * roadmap already assumes.
	 */
	class StaticDepthPass
	{
	public:
		StaticDepthPass() = default;
		~StaticDepthPass() noexcept { logger::trace("~StaticDepthPass"); }

		StaticDepthPass(const StaticDepthPass&) noexcept = delete;
		StaticDepthPass(StaticDepthPass&&) noexcept      = delete;

		StaticDepthPass&
		operator=(const StaticDepthPass&) noexcept = delete;

		StaticDepthPass&
		operator=(StaticDepthPass&&) noexcept = delete;

		void
		Release()
		{
			m_HardwareCullKernel.Reset();
			m_MaterialCullKernel.Reset();
			for (MeshletKernel& kernel : m_CoverageKernels)
			{
				kernel.Reset();
			}
		}

		void
		Init(IDevice* device, PipelineBatch& pipelines, const DrawBucketTable& buckets);

		/**
		 * Requests the coverage kernels for the buckets set in `buckets` that are not already
		 * initialized; they are live once `pipelines` is built. Buckets with no coverage kernel
		 * are ignored.
		 */
		void
		AddDrawBucketKernels(
			IDevice*              device,
			PipelineBatch&        pipelines,
			const DrawBucketMask& buckets);

		/** @pre the batch Init requested into has been built. Fatal on a binder name the PSO lacks. */
		void
		CheckBindings() const;

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

	private:
		static void
		BindKernel(MeshletKernel& kernel, const DrawData& draw, const PassContext& resources);

		void
		Execute(const DrawData& draw, const PassContext& resources);

		// The opaque buckets need no pixel stage beyond depth, so they share two pipelines split
		// by how DrawBucketCullMode culls each bucket: in hardware (the material kinds with no
		// doubleSided flag), or not at all, leaving back faces to the mesh stage. The coverage
		// kernels each pair the same geometry stage with their own discard-only pixel stage --
		// indexed by bucket id, grown with the table, empty except at static cutout and hashed
		// buckets.
		MeshletKernel              m_HardwareCullKernel;
		MeshletKernel              m_MaterialCullKernel;
		std::vector<MeshletKernel> m_CoverageKernels;

		const DrawBucketTable* m_DrawBuckets = nullptr;
	};
}
