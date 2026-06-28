#pragma once
#include "pipeline/MeshletKernel.h"

namespace bgl
{
	class IDevice;
	class FrameGraph;
	class PassContext;

	struct DrawData;

	class GBufferPass
	{
	public:
		GBufferPass() = default;
		~GBufferPass() noexcept { logger::trace("~GBufferPass"); }
		GBufferPass(IDevice* device) { Init(device); }

		GBufferPass(const GBufferPass&) noexcept = delete;
		GBufferPass(GBufferPass&&) noexcept      = delete;

		GBufferPass&
		operator=(const GBufferPass&) noexcept = delete;

		GBufferPass&
		operator=(GBufferPass&&) noexcept = delete;

		void
		Release()
		{
			m_Kernel.Reset();
		}

		void
		Init(IDevice* device);

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

		void
		Execute(const DrawData& draw, const PassContext& resources);

	private:
		struct SceneBuffer
		{
			std::string_view graphName;
			std::string_view uniformKey;
			bool             required;
		};

		static constexpr std::array<SceneBuffer, 8> c_SceneBuffers = { {
			{ "scene.instanceBuffer", "instanceBuffer", true },
			{ "scene.meshInstanceBuffer", "meshBuffer", false },
			{ "scene.geomBuffer", "geomBuffer", false },
			{ "scene.meshletBuffer", "meshletBuffer", true },
			{ "scene.vertexMapBuffer", "vertexMapBuffer", true },
			{ "scene.vertexBuffer", "vertexBuffer", true },
			{ "scene.indexBuffer", "indexBuffer", true },
			{ "scene.compactedInstances", "compactedInstances", true },
		} };

		MeshletKernel m_Kernel;
	};
}
