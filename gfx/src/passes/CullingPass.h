#pragma once

#include "buffer/DynamicConstantBuffer.h"
#include "buffer/StructuredBufferGPU.h"
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
		StructuredBufferGPU<DrawIndexedArgs> m_drawIndirectArgsBuffer;
		StructuredBufferGPU<uint32_t>        m_drawIndirectCountBuffer;
		StructuredBufferGPU<uint32_t>        m_meshVisibleCountBuffer;
		StructuredBufferGPU<uint32_t>        m_meshInstanceOffsetBuffer;
		StructuredBufferGPU<uint32_t>        m_meshWriteCursor;
		StructuredBufferGPU<Mesh::Instance>  m_compactedInstanceBuffer;

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
		nvrhi::BindingLayoutHandle   m_computeBindingLayout;
		nvrhi::BindingSetHandle      m_computeBindingSet;
		nvrhi::BindingLayoutHandle   m_drawBindingLayout;
		nvrhi::BindingSetHandle      m_drawBindingSet;
		nvrhi::BindingSetItem        m_frameConstantsBindingSetItem;
	};
}
