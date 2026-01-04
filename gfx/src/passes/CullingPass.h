#pragma once

#include "buffer/DynamicConstantBuffer.h"
#include "buffer/StructuredBufferUAV.h"
#include "mesh/DrawIndexedArgs.h"

class FrameGraph;
class FrameGraphBlackboard;

namespace gfx
{
	class MeshRegistry;
	class Camera;

	class CullingPass
	{
	public:
		CullingPass() = default;

		void
		Init(
			MeshRegistry&       registry,
			nvrhi::DeviceHandle device,
			uint32_t            screenW,
			uint32_t            screenH);

		void
		AttachToFrameGraph(
			MeshRegistry&         registry,
			uint32_t              screenW,
			uint32_t              screenH,
			FrameGraph&           frameGraph,
			FrameGraphBlackboard& blackBoard,
			nvrhi::DeviceHandle   device,
			Camera&               camera);

	private:
		void
		CreateBindingSet(MeshRegistry& registry, nvrhi::DeviceHandle device);

	private:
		DynamicConstantBuffer                m_frameConstants;
		StructuredBufferUAV<DrawIndexedArgs> m_drawIndirectBuffer;
		StructuredBufferUAV<uint32_t>        m_drawIndirectBufferCounter;
		nvrhi::CommandListHandle             m_cmdList;
		nvrhi::ShaderHandle                  m_cullingCS;
		nvrhi::ComputePipelineHandle         m_computePipeline;
		nvrhi::BindingLayoutHandle           m_bindingLayout;
		nvrhi::BindingSetHandle              m_bindingSet;
		nvrhi::BindingSetItem                m_cameraBindingSetItem;
	};
}
