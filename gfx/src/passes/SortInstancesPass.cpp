#include "passes/SortInstancesPass.h"
#include "BindingSlots.h"
#include "passes/output/FrameData.h"
#include "passes/output/SortedInstancesData.h"
#include "shader_util/util.h"
#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace gfx
{
	nvrhi::BindingLayoutHandle
	SortInstancesPass::Init(nvrhi::BindingLayoutHandle blFrameData, nvrhi::DeviceHandle device)
	{
		m_mainCommandList = device->createCommandList();

		g_binInstanceOffsets = ComputeBufferDesc{}
		                           .SetElement<uint32_t>()
		                           .SetInitialState(nvrhi::ResourceStates::ShaderResource)
		                           .SetKeepInitialState()
		                           .SetElementCount(1)
		                           .SetName("GroupOffsets")
		                           .Create(device);

		{
			struct InstanceAndSortKey
			{
				uint32_t instance;
				uint64_t sortKey;
			};

			for (auto i : { 0, 1 })
			{
				auto name            = std::format("GroupedInstances{}", i);
				m_sortedInstances[i] = ComputeBufferDesc{}
				                           .SetElement<InstanceAndSortKey>()
				                           .SetInitialState(nvrhi::ResourceStates::ShaderResource)
				                           .SetElementCount(1)
				                           .SetName(name)
				                           .Create(device);
			}
		}
		auto desc = DynamicConstantBufferDesc{};
		desc.AddElement("arg0", ElementType::kUInt).SetName("Push Constants").SetIsPushConstant();

		m_sortInstancesConstants = DynamicConstantBuffer(device, desc);

		{
			auto bindingLayoutDesc = nvrhi::BindingLayoutDesc{};

			bindingLayoutDesc.setRegisterSpace(BindingSpaces::SortInstancesSpace)
				.addItem(
					nvrhi::BindingLayoutItem::PushConstants(BindingSlots::CB::SortConstants, 16))
				.addItem(
					nvrhi::BindingLayoutItem::StructuredBuffer_UAV(BindingSlots::UAV::GroupOffsets))
				.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
				.setVisibility(nvrhi::ShaderType::All);

			m_blGroupOffsets = device->createBindingLayout(bindingLayoutDesc);
		}

		{
			auto shader = createShaderFromFile(
				device,
				"shaders/CS_Histogram.cso",
				nvrhi::ShaderType::Compute,
				"GroupOffsets");

			m_histogramPipeline = device->createComputePipeline(
				nvrhi::ComputePipelineDesc{}
					.setComputeShader(shader)
					.addBindingLayout(blFrameData)
					.addBindingLayout(m_blGroupOffsets));
		}

		{
			auto shader = createShaderFromFile(
				device,
				"shaders/CS_PrefixSum.cso",
				nvrhi::ShaderType::Compute,
				"PrefixSum");

			m_prefixSumPipeline = device->createComputePipeline(
				nvrhi::ComputePipelineDesc{}.setComputeShader(shader).addBindingLayout(
					m_blGroupOffsets));
		}

		{
			auto bindingLayoutDesc = nvrhi::BindingLayoutDesc{};
			bindingLayoutDesc.setRegisterSpace(BindingSpaces::SortInstancesSpace)
				.addItem(
					nvrhi::BindingLayoutItem::PushConstants(BindingSlots::CB::SortConstants, 16))
				.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
				.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0))
				.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1))
				.setVisibility(nvrhi::ShaderType::All);

			m_blScatterInOut         = device->createBindingLayout(bindingLayoutDesc);
			auto computePipelineDesc = nvrhi::ComputePipelineDesc{};
			computePipelineDesc
				.setComputeShader(createShaderFromFile(
					device,
					"shaders/CS_Scatter.cso",
					nvrhi::ShaderType::Compute,
					"Scatter"))
				.addBindingLayout(blFrameData)
				.addBindingLayout(m_blScatterInOut);
			m_scatterPipeline = device->createComputePipeline(computePipelineDesc);
		}

		auto bindingLayoutDesc = nvrhi::BindingLayoutDesc{};
		bindingLayoutDesc.setRegisterSpace(BindingSpaces::SortInstancesSpace)
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1))
			.setVisibility(nvrhi::ShaderType::All);

		m_blOut = device->createBindingLayout(bindingLayoutDesc);

		return m_blOut;
	}

	void
	SortInstancesPass::CreateBindingSet(nvrhi::DeviceHandle device)
	{
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		for (auto i : { 0, 1 })
		{
			auto bindingSetDesc = nvrhi::BindingSetDesc{};
			bindingSetDesc
				.addItem(
					m_sortInstancesConstants.GetBindingSetItem(BindingSlots::CB::SortConstants))
				.addItem(g_binInstanceOffsets.GetBindingSetItemUAV(BindingSlots::UAV::GroupOffsets))
				.addItem(m_sortedInstances[i].GetBindingSetItemSRV(0));

			m_bsGroupOffsets[i] = device->createBindingSet(bindingSetDesc, m_blGroupOffsets);
		}

		for (auto i : { 0, 1 })
		{
			auto bindingSetDesc = nvrhi::BindingSetDesc{};
			bindingSetDesc
				.addItem(
					m_sortInstancesConstants.GetBindingSetItem(BindingSlots::CB::SortConstants))
				.addItem(m_sortedInstances[i].GetBindingSetItemSRV(0))
				.addItem(m_sortedInstances[i ^ 1].GetBindingSetItemUAV(0))
				.addItem(g_binInstanceOffsets.GetBindingSetItemSRV(1));

			m_bsScatterInOut[i] = device->createBindingSet(bindingSetDesc, m_blScatterInOut);
		}

		auto bindingSetDesc = nvrhi::BindingSetDesc{};
		bindingSetDesc.addItem(m_sortedInstances[0].GetBindingSetItemSRV(0))
			.addItem(g_binInstanceOffsets.GetBindingSetItemSRV(1));

		m_bsOut = device->createBindingSet(bindingSetDesc, m_blOut);
	}

	bool
	SortInstancesPass::Update(uint32_t instances)
	{
		auto instancesAligned = alignNext(instances, 256u);
		bool updated          = false;

		if (instancesAligned > g_binInstanceOffsets.Size())
		{
			g_binInstanceOffsets.Resize(instancesAligned * 2);
			g_binInstanceOffsets.Update(m_mainCommandList, m_mainCommandList->getDevice());

			for (auto i : { 0, 1 })
			{
				m_sortedInstances[i].Resize(instancesAligned * 2);
				m_sortedInstances[i].Update(m_mainCommandList, m_mainCommandList->getDevice());
			}

			updated = true;
		}

		return updated;
	}

	void
	SortInstancesPass::Histogram(
		BindingSetView bsPerFrame,
		uint32_t       numGroups,
		uint32_t       bitshift,
		uint32_t       pingPong)
	{
		constexpr auto histogramGroups = 256;
		auto           computeState    = nvrhi::ComputeState{};
		computeState.setPipeline(m_histogramPipeline);

		bsPerFrame.AttachBindingSetTo(computeState);
		computeState.addBindingSet(m_bsGroupOffsets[pingPong]);

		m_mainCommandList->setComputeState(computeState);

		// arg0 is bit shift
		m_sortInstancesConstants["arg0"] = bitshift;

		m_sortInstancesConstants.Update(m_mainCommandList);
		m_mainCommandList->dispatch(numGroups);
	}

	void
	SortInstancesPass::PrefixSum(uint32_t numGroups, uint32_t pingPong)
	{
		auto computeState = nvrhi::ComputeState{};
		computeState.setPipeline(m_prefixSumPipeline).addBindingSet(m_bsGroupOffsets[pingPong]);

		m_mainCommandList->setComputeState(computeState);

		m_sortInstancesConstants["arg0"] = numGroups;
		m_sortInstancesConstants.Update(m_mainCommandList);

		m_mainCommandList->dispatch(numGroups);
	}

	void
	SortInstancesPass::Scatter(
		BindingSetView bsPerFrame,
		uint32_t       numGroups,
		uint32_t       bitShift,
		uint32_t       pingPong)
	{
		auto  computeState = nvrhi::ComputeState{};
		auto& bindingSet   = m_bsScatterInOut[pingPong];

		computeState.setPipeline(m_scatterPipeline);
		bsPerFrame.AttachBindingSetTo(computeState);
		computeState.addBindingSet(bindingSet);

		m_mainCommandList->setComputeState(computeState);

		m_sortInstancesConstants["arg0"] = bitShift;
		m_sortInstancesConstants.Update(m_mainCommandList);

		m_mainCommandList->dispatch(numGroups);
	}

	void
	SortInstancesPass::AttachToFrameGraph(
		FrameGraph&           frameGraph,
		FrameGraphBlackboard& blackBoard,
		nvrhi::DeviceHandle   device,
		uint64_t              frameIdx)
	{
		auto frameData = blackBoard.get<FrameData>();

		blackBoard.add<SortedInstancesData>() = frameGraph.addCallbackPass<SortedInstancesData>(
			"SortInstancesPass",
			[=](FrameGraph::Builder& builder, SortedInstancesData& sortInstancesData) {
				builder.read(frameData.bsFrameData);
				builder.read(frameData.instanceCount);

				sortInstancesData.bsSortedInstances =
					builder.create<FGBindingSet>("bsSortedInstances"sv, {});

				builder.setSideEffect();
			},
			[=](const SortedInstancesData& data, FrameGraphPassResources& res, void*) {
				auto& bsFrameData   = res.get<FGBindingSet>(frameData.bsFrameData).Get();
				auto  instanceCount = res.get<FGCount>(frameData.instanceCount).Get();

				m_mainCommandList->open();

				if (Update(instanceCount) || frameIdx == 0)
				{
					CreateBindingSet(device);
				}

				g_binInstanceOffsets.TrackResourceState(
					m_mainCommandList,
					nvrhi::ResourceStates::ShaderResource);

				m_sortedInstances[0].TrackResourceState(
					m_mainCommandList,
					nvrhi::ResourceStates::ShaderResource);

				m_sortedInstances[1].TrackResourceState(
					m_mainCommandList,
					nvrhi::ResourceStates::ShaderResource);

				constexpr auto instancesPerGroup = 256;
				auto numGroups = (instanceCount + instancesPerGroup - 1) / instancesPerGroup;

				for (auto bitShift = 0u, pingPong = 0u; bitShift < 64; bitShift += 8)
				{
					g_binInstanceOffsets.TransitionState(
						m_mainCommandList,
						nvrhi::ResourceStates::UnorderedAccess);

					// For the first iteration, the input is already in the correct state
					if (bitShift != 0)
					{
						m_sortedInstances[pingPong].TransitionState(
							m_mainCommandList,
							nvrhi::ResourceStates::ShaderResource);
					}

					m_sortedInstances[pingPong ^ 1].TransitionState(
						m_mainCommandList,
						nvrhi::ResourceStates::UnorderedAccess);

					Histogram(bsFrameData, numGroups, bitShift, pingPong);

					PrefixSum(numGroups, pingPong);

					g_binInstanceOffsets.TransitionState(
						m_mainCommandList,
						nvrhi::ResourceStates::ShaderResource);

					Scatter(bsFrameData, numGroups, bitShift, pingPong);

					pingPong = 1 - pingPong;
				}

				m_sortedInstances[0].TransitionState(
					m_mainCommandList,
					nvrhi::ResourceStates::ShaderResource);

				res.get<FGBindingSet>(data.bsSortedInstances).SetValue(m_mainCommandList, m_bsOut);

				m_mainCommandList->commitBarriers();

				m_mainCommandList->close();
				device->executeCommandList(m_mainCommandList);
			});
	}

}
