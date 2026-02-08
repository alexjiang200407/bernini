#pragma once
#include "BindingSlots.h"
#include "buffer/ComputeBuffer.h"
#include "buffer/DynamicConstantBuffer.h"
#include "draw_instance/MeshletDispatchArg.h"
#include "draw_instance/PSO.h"
#include "frame_graph/FrameGraphView.h"
#include "passes/output/FrameData.h"
#include "passes/output/SortedInstancesData.h"
#include "shader_util/util.h"
#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

class FrameGraph;
class FrameGraphBlackboard;

namespace gfx
{
	struct FrameData;

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
			nvrhi::BindingLayoutHandle blSortedInstances)
		{
			m_drawArgs = ComputeBufferDesc{}
			                 .SetElement<MeshletDispatchArg>()
			                 .SetElementCount(PSO_COUNT)
			                 .SetInitialState(nvrhi::ResourceStates::IndirectArgument)
			                 .SetName("GBufferPass Draw Args")
			                 .Create(device);

			m_binMeshletCounts = ComputeBufferDesc{}
			                         .SetElement<uint32_t>()
			                         .SetElementCount(PSO_COUNT)
			                         .SetInitialState(nvrhi::ResourceStates::ShaderResource)
			                         .SetName("Meshlet Counts Per Bin")
			                         .Create(device);

			m_cmdList = device->createCommandList();

			{
				auto blDesc = nvrhi::BindingLayoutDesc{};
				blDesc.setRegisterSpace(BindingSpaces::GBufferSpace)
					.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0))
					.setVisibility(nvrhi::ShaderType::All);

				auto blCountMeshlets = device->createBindingLayout(blDesc);

				auto bsDesc = nvrhi::BindingSetDesc{};
				bsDesc.addItem(m_binMeshletCounts.GetBindingSetItemUAV(0));
				m_bsCountMeshlets = device->createBindingSet(bsDesc, blCountMeshlets);

				auto computePipelineDesc = nvrhi::ComputePipelineDesc{};
				computePipelineDesc.addBindingLayout(blPerFrame)
					.addBindingLayout(blSortedInstances)
					.addBindingLayout(blCountMeshlets)
					.setComputeShader(createShaderFromFile(
						device,
						"shaders/CS_GBuffer_CountMeshletsPerBin.cso",
						nvrhi::ShaderType::Compute,
						"CS_GBuffer_CountMeshletsPerBin"));

				m_countMeshletsPipeline = device->createComputePipeline(computePipelineDesc);
			}

			{
				auto blDesc = nvrhi::BindingLayoutDesc{};
				blDesc.setRegisterSpace(BindingSpaces::GBufferSpace)
					.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0))
					.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
					.setVisibility(nvrhi::ShaderType::All);

				auto blGenDispatchArgs = device->createBindingLayout(blDesc);

				auto bsDesc = nvrhi::BindingSetDesc{};
				bsDesc.addItem(m_drawArgs.GetBindingSetItemUAV(0))
					.addItem(m_binMeshletCounts.GetBindingSetItemSRV(0));
				m_bsGenDispatchArgs = device->createBindingSet(bsDesc, blGenDispatchArgs);

				auto computePipelineDesc = nvrhi::ComputePipelineDesc{};
				computePipelineDesc.addBindingLayout(blGenDispatchArgs)
					.setComputeShader(createShaderFromFile(
						device,
						"shaders/CS_GBuffer_GenDispatchArgs.cso",
						nvrhi::ShaderType::Compute,
						"CS_GBuffer_GenDispatchArgs"));

				m_genDispatchArgsPipeline = device->createComputePipeline(computePipelineDesc);
			}
		}

		void
		CountMeshletsPerPSO(
			const BindingSetView& bsFrameData,
			const BindingSetView& bsSortedInstances,
			uint32_t              instanceCount)
		{
			auto state = nvrhi::ComputeState{};
			state.setPipeline(m_countMeshletsPipeline);

			bsFrameData.AttachBindingSetTo(state);
			bsSortedInstances.AttachBindingSetTo(state);
			state.addBindingSet(m_bsCountMeshlets);

			m_cmdList->setComputeState(state);
			m_cmdList->dispatch(instanceCount);
		}

		void
		BuildDrawArgs()
		{
			auto state = nvrhi::ComputeState{};
			state.setPipeline(m_genDispatchArgsPipeline).addBindingSet(m_bsGenDispatchArgs);

			m_cmdList->setComputeState(state);
			m_cmdList->dispatch(PSO_COUNT);
		}

		void
		AttachToFrameGraph(
			FrameGraph&           frameGraph,
			FrameGraphBlackboard& blackBoard,
			nvrhi::DeviceHandle   device)
		{
			auto frameData        = blackBoard.get<FrameData>();
			auto sortInstanceData = blackBoard.get<SortedInstancesData>();

			frameGraph.addCallbackPass(
				"GBufferPass",
				[this, device, frameData, sortInstanceData](
					FrameGraph::Builder& builder,
					auto&                data) {
					builder.read(frameData.bsFrameData);
					builder.read(frameData.instanceCount);
					builder.read(sortInstanceData.bsSortedInstances);

					builder.setSideEffect();
				},
				[this, device, &blackBoard, sortInstanceData, frameData](
					const auto&,
					FrameGraphPassResources& res,
					void*) {
					m_cmdList->open();

					auto  instanceCount = res.get<FGCount>(frameData.instanceCount).Get();
					auto& bsSortedInstances =
						res.get<FGBindingSet>(sortInstanceData.bsSortedInstances).Get();
					auto& bsFrameData = res.get<FGBindingSet>(frameData.bsFrameData).Get();

					bsFrameData.TrackResources(m_cmdList);
					bsSortedInstances.TrackResources(m_cmdList);

					m_binMeshletCounts.TrackResourceState(
						m_cmdList,
						nvrhi::ResourceStates::ShaderResource);
					m_drawArgs.TrackResourceState(
						m_cmdList,
						nvrhi::ResourceStates::IndirectArgument);

					m_binMeshletCounts.TransitionState(
						m_cmdList,
						nvrhi::ResourceStates::UnorderedAccess);

					m_binMeshletCounts.Clear(m_cmdList);

					CountMeshletsPerPSO(bsFrameData, bsSortedInstances, instanceCount);

					m_drawArgs.TransitionState(m_cmdList, nvrhi::ResourceStates::UnorderedAccess);
					m_binMeshletCounts.TransitionState(
						m_cmdList,
						nvrhi::ResourceStates::ShaderResource);

					BuildDrawArgs();

					m_drawArgs.TransitionState(m_cmdList, nvrhi::ResourceStates::IndirectArgument);

					m_cmdList->close();

					device->executeCommandList(m_cmdList);
				});
		}

	private:
		nvrhi::CommandListHandle     m_cmdList;
		ComputeBuffer                m_drawArgs;
		ComputeBuffer                m_binMeshletCounts;
		nvrhi::ComputePipelineHandle m_countMeshletsPipeline;
		nvrhi::ComputePipelineHandle m_genDispatchArgsPipeline;
		nvrhi::BindingSetHandle      m_bsCountMeshlets;
		nvrhi::BindingSetHandle      m_bsGenDispatchArgs;
	};
}
