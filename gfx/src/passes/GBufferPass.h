#pragma once
#include "buffer/ComputeBuffer.h"
#include "buffer/DynamicConstantBuffer.h"
#include "frame_graph/FrameGraphView.h"
#include "types/MeshletDispatchArg.h"
#include "types/PSO.h"

class FrameGraph;
class FrameGraphBlackboard;

namespace gfx
{
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
		Init(
			nvrhi::DeviceHandle        device,
			nvrhi::BindingLayoutHandle blPerFrame,
			nvrhi::BindingLayoutHandle blSortedInstances,
			nvrhi::FramebufferInfo     outBufferInfo);

		void
		BuildDrawArgs(BindingSetView bsSortData);

		void
		DispatchMeshlets(
			BindingSetView           bsFrameData,
			BindingSetView           bsSortedData,
			nvrhi::FramebufferHandle frameBuffer,
			nvrhi::ViewportState     vpState);

		void
		SetPipeline(PSO pso, nvrhi::MeshletPipelineHandle pipeline);

		void
		AttachToFrameGraph(
			FrameGraph&              frameGraph,
			FrameGraphBlackboard&    blackBoard,
			nvrhi::DeviceHandle      device,
			nvrhi::FramebufferHandle frameBuffer,
			uint32_t                 screenWidth,
			uint32_t                 screenHeight);

		void
		AddDrawStrategy(
			nvrhi::DeviceHandle        device,
			PSO                        pso,
			nvrhi::FramebufferInfo     outBufferInfo,
			const std::string&         ampShader,
			const std::string&         meshShader,
			const std::string&         pixShader,
			nvrhi::RenderState         renderState,
			nvrhi::BindingLayoutHandle blDrawArgs);

	private:
		nvrhi::CommandListHandle     m_cmdList;
		DynamicConstantBuffer        m_gBufferRootConstants;
		ComputeBuffer                m_drawArgs;
		nvrhi::BindingLayoutHandle   m_blPerFrame;
		nvrhi::BindingLayoutHandle   m_blSortedInstances;
		nvrhi::BindingLayoutHandle   m_blDrawArgs;
		nvrhi::ComputePipelineHandle m_genDispatchArgsPipeline;
		nvrhi::BindingSetHandle      m_bsGenDispatchArgs;
		nvrhi::BindingSetHandle      m_bsDrawArgs;
		nvrhi::MeshletPipelineHandle m_meshletPipeline[PSO_COUNT]{};
	};
}
