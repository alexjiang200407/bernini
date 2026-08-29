#pragma once
#include "gfx/PipelineBuild.h"
#include "pipeline/MeshletKernel.h"

namespace bgl
{
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

		static constexpr uint32_t c_Pipelines = 1;

		void
		Init(IDevice* device, PipelineBuild& build);

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

		void
		Execute(const DrawData& draw, const PassContext& resources);

	private:
		MeshletKernel m_Kernel;
	};
}
