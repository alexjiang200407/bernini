#pragma once
#include "passes/PassInitContext.h"
#include <bgpu/pipeline/MeshletKernel.h>
#include <spdlog/spdlog.h>

namespace bgpu
{}

namespace bgpu
{
	class PipelineBatch;
	class IDevice;
}

namespace bgl
{

	class FrameGraph;
	class PassContext;
	struct DrawData;

	class SkyboxPass
	{
	public:
		SkyboxPass() = default;
		~SkyboxPass() noexcept { spdlog::trace("~SkyboxPass"); }

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
		Init(const PassInitContext& ctx);

		/** @pre the batch Init requested into has been built. Fatal on a binder name the PSO lacks. */
		void
		CheckBindings() const;

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

		void
		Execute(const DrawData& draw, const PassContext& resources);

	private:
		bgpu::MeshletKernel m_Kernel;
	};
}
