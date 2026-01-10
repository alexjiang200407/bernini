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

	struct MeshInstance
	{
		float    modelTransform[16];  // float4x4 = 16 floats = 64 bytes
		uint32_t infoID;              // 4 bytes
	};

	// Hard guarantee layout correctness
	static_assert(sizeof(MeshInstance) == 68, "MeshInstance size must match HLSL");

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
		//StructuredBufferUAV<DrawIndexedArgs> m_drawIndirectBuffer;
		//StructuredBufferUAV<uint32_t>        m_drawIndirectBufferCounter;
		StructuredBufferUAV<DrawIndexedArgs> m_drawIndirectArgsBuffer;
		StructuredBufferUAV<uint32_t>        m_drawIndirectCountBuffer;
		DynamicConstantBuffer                m_frameConstants;
		StructuredBufferUAV<uint32_t>        m_visibleInstanceCount;
		StructuredBufferUAV<MeshInstance>    m_visibleInstanceBuffer;
		nvrhi::CommandListHandle             m_cmdList;
		nvrhi::ShaderHandle                  m_cullingCS;
		nvrhi::ShaderHandle                  m_buildArgsCS;
		nvrhi::ComputePipelineHandle         m_computePipeline;
		nvrhi::ComputePipelineHandle         m_buildArgsPipeline;
		nvrhi::BindingLayoutHandle           m_bindingLayout;
		nvrhi::BindingSetHandle              m_bindingSet;
		nvrhi::BindingSetItem                m_cameraBindingSetItem;
	};
}
