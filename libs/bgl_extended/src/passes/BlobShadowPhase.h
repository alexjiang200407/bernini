#pragma once
#include "pipeline/MeshletKernel.h"
#include <spdlog/spdlog.h>

namespace bgl
{
	class PipelineBatch;

	class IDevice;
	class PassContext;

	struct DrawData;
	struct PassDesc;

	/**
	 * The blob-shadow phase of the Forward pass: one workgroup per placement carrying a blob
	 * shadow, each emitting a disc flattened onto the scene's ground plane.
	 *
	 * A phase and not a frame-graph pass of its own, because it must draw between the opaque
	 * buckets and the transparent phase -- discs depth-test against the opaques, and smoke over a
	 * unit composites over its shadow -- and those are phases of one pass sharing one depth
	 * attachment. ForwardPass owns it and calls it there; only the file is its own.
	 */
	class BlobShadowPhase
	{
	public:
		BlobShadowPhase() = default;
		~BlobShadowPhase() noexcept { logger::trace("~BlobShadowPhase"); }

		BlobShadowPhase(const BlobShadowPhase&) noexcept = delete;
		BlobShadowPhase(BlobShadowPhase&&) noexcept      = delete;

		BlobShadowPhase&
		operator=(const BlobShadowPhase&) noexcept = delete;

		BlobShadowPhase&
		operator=(BlobShadowPhase&&) noexcept = delete;

		void
		Release()
		{
			m_Kernel.Reset();
		}

		void
		Init(IDevice* device, PipelineBatch& pipelines);

		/** @pre the batch Init requested into has been built. Fatal on a binder name the PSO lacks. */
		void
		CheckBindings() const;

		/** Adds the buffers Draw reads to the owning pass's declaration. */
		static void
		DeclareResources(PassDesc& desc);

		/** Records the discs; a view with no blob shadows records nothing. */
		void
		Draw(const DrawData& draw, const PassContext& resources);

	private:
		MeshletKernel m_Kernel;
	};
}
