#pragma once
#include "buffer/DynamicBuffers.h"

class FrameGraph;
class FrameGraphBlackboard;

namespace gfx
{
	class SortInstancesPass final
	{
	public:
		void
		Init(nvrhi::BindingLayoutHandle blFrameData, nvrhi::DeviceHandle device);

		void
		AttachToFrameGraph(
			FrameGraph&           frameGraph,
			FrameGraphBlackboard& blackBoard,
			nvrhi::DeviceHandle   device,
			bool                  isFinalPass = false);

	private:
		void
		CreateBindingSet(nvrhi::DeviceHandle device);

		bool
		Update(uint32_t meshletInstances);

		void
		Histogram(
			nvrhi::BindingSetHandle bsPerFrame,
			uint32_t                numGroups,
			uint32_t                bitshift,
			uint32_t                pingPong);

		void
		PrefixSum(uint32_t meshInstances, uint32_t pingPong);

		void
		Scatter(
			nvrhi::BindingSetHandle bsPerFrame,
			uint32_t                numGroups,
			uint32_t                bitShift,
			uint32_t                pingPong);

	private:
		nvrhi::CommandListHandle     m_mainCommandList;
		DynamicConstantBuffer        m_sortInstancesConstants;
		ComputeBuffer                m_groupedInstances[2];
		ComputeBuffer                m_groupOffsets;
		nvrhi::ComputePipelineHandle m_histogramPipeline;
		nvrhi::ComputePipelineHandle m_prefixSumPipeline;
		nvrhi::ComputePipelineHandle m_scatterPipeline;
		nvrhi::BindingLayoutHandle   m_blGroupOffsets;
		nvrhi::BindingLayoutHandle   m_blScatterInOut;
		nvrhi::BindingSetHandle      m_bsGroupOffsets[2];
		nvrhi::BindingSetHandle      m_bsScatterInOut[2];
	};
}
