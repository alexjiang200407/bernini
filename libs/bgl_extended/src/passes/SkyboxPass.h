#pragma once
#include "pipeline/MeshletKernel.h"
#include <spdlog/spdlog.h>

namespace bgl
{
	class PipelineBatch;

	class IDevice;
	class FrameGraph;
	class PassContext;
	struct DrawData;

	class SkyboxPass
	{
	public:
		SkyboxPass() = default;
		~SkyboxPass() noexcept { logger::trace("~SkyboxPass"); }

		SkyboxPass(const SkyboxPass&) noexcept = delete;
		SkyboxPass(SkyboxPass&&) noexcept      = delete;

		SkyboxPass&
		operator=(const SkyboxPass&) noexcept = delete;

		SkyboxPass&
		operator=(SkyboxPass&&) noexcept = delete;

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

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

		void
		Execute(const DrawData& draw, const PassContext& resources);

	private:
		MeshletKernel m_Kernel;
	};
}
