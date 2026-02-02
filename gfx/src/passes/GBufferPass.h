#pragma once
#include "buffer/ComputeBuffer.h"
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

		bool
		Update(uint32_t meshletInstances);

		void
		AttachToFrameGraph(
			FrameGraph&           frameGraph,
			FrameGraphBlackboard& blackBoard,
			MeshRegistry&         registry,
			Camera&               camera,
			RenderArgs            renderArgs);

	private:
		nvrhi::CommandListHandle     m_mainCommandList;
		DynamicConstantBuffer        m_frameConstants;
		DynamicConstantBuffer        m_indirectDrawPushConstants;
		ComputeBuffer                m_indirectDrawArguments;
		ComputeBuffer                m_visibleMeshletIndices;
		nvrhi::MeshletPipelineHandle m_pipeline;
		nvrhi::ShaderHandle          m_meshShader;
		nvrhi::ShaderHandle          m_ampShader;
		nvrhi::ShaderHandle          m_pixelShader;
		nvrhi::BindingLayoutHandle   m_perFrameBindingLayout;
		nvrhi::BindingLayoutHandle   m_indirectDrawDataBindingLayout;
		nvrhi::BindingSetHandle      m_perFrameBindingSet;
		nvrhi::BindingSetHandle      m_indirectDrawDataBindingSet;
	};
}
