#pragma once
#include "pipeline/MeshletKernel.h"
#include <array>
#include <bgl/MaterialType.h>
#include <spdlog/spdlog.h>

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
			m_Kernel.Reset();
			for (MeshletKernel& kernel : m_CoverageKernels)
			{
				kernel.Reset();
			}
		}

		void
		Init(IDevice* device, PipelineBatch& pipelines);

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

		// One pipeline serves every opaque static bucket; the coverage buckets each pair the same
		// geometry stage with their own discard-only pixel stage, one kernel per static cutout and
		// hashed row (engine PBR and loose, then each game slot's pair).
		static constexpr uint32_t c_CoverageKernelCount = 4 + 2 * cGameSlots;

		MeshletKernel                                    m_Kernel;
		std::array<MeshletKernel, c_CoverageKernelCount> m_CoverageKernels;
	};
}
