#pragma once

#include "buffer/DynamicConstantBuffer.h"
#include "buffer/StructuredBufferUAV.h"
#include "mesh/DrawIndexedArgs.h"
#include "mesh/Mesh.h"

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
		StructuredBufferUAV<DrawIndexedArgs> m_drawIndirectArgsBuffer;
		StructuredBufferUAV<uint32_t>        m_drawIndirectCountBuffer;
		StructuredBufferUAV<uint32_t>        m_meshVisibleCountBuffer;
		StructuredBufferUAV<uint32_t>        m_meshInstanceOffsetBuffer;
		StructuredBufferUAV<uint32_t>        m_meshWriteCursor;
		StructuredBufferUAV<Mesh::Instance>  m_compactedInstanceBuffer;
		//StructuredBufferSRV<Mesh::Instance>  m_compactedInstanceBufferSRV;

		DynamicConstantBuffer        m_frameConstants;
		nvrhi::CommandListHandle     m_cmdList;
		nvrhi::ShaderHandle          m_cullHistogramCS;
		nvrhi::ShaderHandle          m_prefixSumCS;
		nvrhi::ShaderHandle          m_cullScatterCS;
		nvrhi::ShaderHandle          m_buildArgsCS;
		nvrhi::ComputePipelineHandle m_histogramPipeline;
		nvrhi::ComputePipelineHandle m_prefixSumPipeline;
		nvrhi::ComputePipelineHandle m_scatterPipeline;
		nvrhi::ComputePipelineHandle m_buildArgsPipeline;
		nvrhi::BindingLayoutHandle   m_bindingLayout;
		nvrhi::BindingSetHandle      m_bindingSet;
		nvrhi::BindingSetItem        m_cameraBindingSetItem;
	};
}
