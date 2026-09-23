#pragma once
#include "passes/PassInitContext.h"
#include "pipeline/MeshletKernel.h"
#include <spdlog/spdlog.h>

namespace bgl
{
	class PipelineBatch;

	class FrameGraph;
	class IDevice;
	class PassContext;

	struct DrawData;

	/**
	 * One workgroup per placement carrying a blob shadow, each emitting a screen-space decal that
	 * drapes over whatever static surface the StaticDepth pass recorded beneath the caster.
	 *
	 * Attached between Forward's world and skinned phases, so a unit drawn over the decal hides
	 * it and a transparent composites over it.
	 */
	class BlobShadowPass
	{
	public:
		BlobShadowPass() = default;
		~BlobShadowPass() noexcept { logger::trace("~BlobShadowPass"); }

		BlobShadowPass(const BlobShadowPass&) noexcept = delete;
		BlobShadowPass(BlobShadowPass&&) noexcept      = delete;

		BlobShadowPass&
		operator=(const BlobShadowPass&) noexcept = delete;

		BlobShadowPass&
		operator=(BlobShadowPass&&) noexcept = delete;

		void
		Release()
		{
			m_Kernel.Reset();
		}

		void
		Init(const PassInitContext& ctx);

		/** @pre the batch Init requested into has been built. Fatal on a binder name the PSO lacks. */
		void
		CheckBindings() const;

		/** Attaches nothing for a view with no blob shadows. */
		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

	private:
		void
		Execute(const DrawData& draw, const PassContext& resources);

		MeshletKernel m_Kernel;
	};
}
