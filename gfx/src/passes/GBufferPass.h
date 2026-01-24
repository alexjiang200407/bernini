#pragma once
#include "buffer/DynamicConstantBuffer.h"

class FrameGraph;
class FrameGraphBlackboard;

namespace gfx
{
	struct FrameData;
	class MeshRegistry;
	class Camera;

	struct RenderArgs
	{
		float                    screenWidth;
		float                    screenHeight;
		nvrhi::DeviceHandle      device;
		nvrhi::FramebufferHandle outBuffer;
		nvrhi::FramebufferInfo   outBufferInfo;
	};

	class GBufferPass
	{
	public:
		void
		Init(nvrhi::DeviceHandle device, MeshRegistry& registry);

		void
		CreateBindingSet(MeshRegistry& registry, nvrhi::DeviceHandle device);

		void
		AttachToFrameGraph(
			FrameGraph&           frameGraph,
			FrameGraphBlackboard& blackBoard,
			MeshRegistry&         registry,
			Camera&               camera,
			RenderArgs            renderArgs);

	private:
		DynamicConstantBuffer        m_frameConstants;
		nvrhi::BindingSetItem        m_frameConstantsBindingSetItem;
		nvrhi::CommandListHandle     m_mainCommandList;
		nvrhi::MeshletPipelineHandle m_pipeline;
		nvrhi::ShaderHandle          m_meshShader;
		nvrhi::ShaderHandle          m_ampShader;
		nvrhi::ShaderHandle          m_pixelShader;
		nvrhi::BindingLayoutHandle   m_bindingLayout;
		nvrhi::BindingSetHandle      m_bindingSet;
	};
}
