#include "passes/SortInstancesPass.h"
#include "BindingSlots.h"
#include "passes/output/FrameData.h"
#include "shader_util/util.h"
#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace gfx
{
	void
	SortInstancesPass::Init(nvrhi::BindingLayoutHandle blFrameData, nvrhi::DeviceHandle device)
	{
		m_mainCommandList = device->createCommandList();

		m_groupOffsets = ComputeBufferDesc{}
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
				auto name             = std::format("GroupedInstances{}", i);
				m_groupedInstances[i] = ComputeBufferDesc{}
				                            .SetElement<InstanceAndSortKey>()
				                            .SetInitialState(
												i == 0 ? nvrhi::ResourceStates::UnorderedAccess :
														 nvrhi::ResourceStates::ShaderResource)
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
				.addItem(m_groupOffsets.GetBindingSetItemUAV(BindingSlots::UAV::GroupOffsets))
				.addItem(m_groupedInstances[i].GetBindingSetItemSRV(0));

			m_bsGroupOffsets[i] = device->createBindingSet(bindingSetDesc, m_blGroupOffsets);
		}

		for (auto i : { 0, 1 })
		{
			auto bindingSetDesc = nvrhi::BindingSetDesc{};
			bindingSetDesc
				.addItem(
					m_sortInstancesConstants.GetBindingSetItem(BindingSlots::CB::SortConstants))
				.addItem(m_groupedInstances[i].GetBindingSetItemSRV(0))
				.addItem(m_groupedInstances[i ^ 1].GetBindingSetItemUAV(0))
				.addItem(m_groupOffsets.GetBindingSetItemSRV(1));

			m_bsScatterInOut[i] = device->createBindingSet(bindingSetDesc, m_blScatterInOut);
		}
	}

	bool
	SortInstancesPass::Update(uint32_t instances)
	{
		auto instancesAligned = alignNext(instances, 256u);
		bool updated          = false;

		if (instancesAligned > m_groupOffsets.Size())
		{
			m_groupOffsets.Resize(instancesAligned * 2);
			m_groupOffsets.Update(m_mainCommandList, m_mainCommandList->getDevice());

			for (auto i : { 0, 1 })
			{
				m_groupedInstances[i].Resize(instancesAligned * 2);
				m_groupedInstances[i].Update(m_mainCommandList, m_mainCommandList->getDevice());
			}

			updated = true;
		}

		return updated;
	}

	void
	SortInstancesPass::Histogram(
		nvrhi::BindingSetHandle bsPerFrame,
		uint32_t                numGroups,
		uint32_t                bitshift,
		uint32_t                pingPong)
	{
		constexpr auto histogramGroups = 256;
		auto           computeState    = nvrhi::ComputeState{};
		computeState.setPipeline(m_histogramPipeline)
			.addBindingSet(bsPerFrame)
			.addBindingSet(m_bsGroupOffsets[pingPong]);

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
		nvrhi::BindingSetHandle bsPerFrame,
		uint32_t                numGroups,
		uint32_t                bitShift,
		uint32_t                pingPong)
	{
		auto  computeState = nvrhi::ComputeState{};
		auto& bindingSet   = m_bsScatterInOut[pingPong];

		computeState.setPipeline(m_scatterPipeline)
			.addBindingSet(bsPerFrame)
			.addBindingSet(bindingSet);

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
		bool                  isFinalPass)
	{
		auto frameData = blackBoard.get<FrameData>();

		frameGraph.addCallbackPass(
			"SortInstancesPass",
			[=](FrameGraph::Builder& builder, auto&) {
				if (isFinalPass)
					builder.setSideEffect();

				builder.read(frameData.bsFrameData);
				builder.read(frameData.instanceCount);
			},
			[this, frameData, device](const auto&, FrameGraphPassResources& res, void*) {
				auto bsFrameData   = res.get<FGBindingSet>(frameData.bsFrameData).Get();
				auto instanceCount = res.get<FGCount>(frameData.instanceCount).Get();

				m_mainCommandList->open();

				if (Update(instanceCount))
				{
					CreateBindingSet(device);
				}

				m_groupOffsets.TrackResourceState(
					m_mainCommandList,
					nvrhi::ResourceStates::ShaderResource);

				m_groupedInstances[0].TrackResourceState(
					m_mainCommandList,
					nvrhi::ResourceStates::UnorderedAccess);

				m_groupedInstances[1].TrackResourceState(
					m_mainCommandList,
					nvrhi::ResourceStates::ShaderResource);

				constexpr auto instancesPerGroup = 256;
				auto numGroups = (instanceCount + instancesPerGroup - 1) / instancesPerGroup;

				for (auto bitShift = 0u, pingPong = 0u; bitShift < 64; bitShift += 8)
				{
					m_groupOffsets.TransitionState(
						m_mainCommandList,
						nvrhi::ResourceStates::UnorderedAccess);

					m_groupedInstances[pingPong].TransitionState(
						m_mainCommandList,
						nvrhi::ResourceStates::ShaderResource);

					m_groupedInstances[pingPong ^ 1].TransitionState(
						m_mainCommandList,
						nvrhi::ResourceStates::UnorderedAccess);

					Histogram(bsFrameData, numGroups, bitShift, pingPong);

					PrefixSum(numGroups, pingPong);

					m_groupOffsets.TransitionState(
						m_mainCommandList,
						nvrhi::ResourceStates::ShaderResource);

					Scatter(bsFrameData, numGroups, bitShift, pingPong);

					pingPong = 1 - pingPong;
				}

				m_mainCommandList->commitBarriers();

				m_mainCommandList->close();
				device->executeCommandList(m_mainCommandList);
			});
	}

}
