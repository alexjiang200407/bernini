#pragma once
#include "pipeline/MeshletKernel.h"

namespace bgl
{
	class IDevice;
	class IResourceManager;
	class FrameGraph;
	class PassContext;
	struct DrawData;

	class SkyboxPass
	{
	public:
		SkyboxPass() = default;
		~SkyboxPass() noexcept { logger::trace("~SkyboxPass"); }
		SkyboxPass(IDevice* device, core::SharedRef<IResourceManager> resourceManager)
		{
			Init(device, std::move(resourceManager));
		}

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
		Init(IDevice* device, core::SharedRef<IResourceManager> resourceManager);

		// No-op when the view has no skybox bound (draw.skybox.valid == false).
		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

		void
		Execute(const DrawData& draw, const PassContext& resources);

	private:
		MeshletKernel m_Kernel;
	};
}
