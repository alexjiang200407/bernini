#include "passes/CullingPass.h"
#include "BindingSlots.h"
#include "camera/Camera.h"
#include "frame_graph/FrameGraphView.h"
#include "graphics/Graphics.h"
#include "mesh/MeshRegistry.h"
#include "passes/FrameData.h"
#include <core/file/file.h>
#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace gfx
{
	void
	CullingPass::Init(
		MeshRegistry&       registry,
		nvrhi::DeviceHandle device,
		uint32_t            screenW,
		uint32_t            screenH)
	{
		m_drawIndirectArgsBuffer.Init(
			device,
			StructuredBufferUAVDesc{}
				.SetName("DrawIndirectArgs")
				.SetStartingLen(1024)
				.SetIsDrawIndirect()
				.SetInitialState(nvrhi::ResourceStates::IndirectArgument));

		m_drawIndirectCountBuffer.Init(
			device,
			StructuredBufferUAVDesc{}
				.SetName("DrawIndirectCount")
				.SetStartingLen(1u)
				.SetInitialState(nvrhi::ResourceStates::UnorderedAccess)
				.SetKeepInitialState(true));

		m_visibleInstanceBuffer.Init(
			device,
			StructuredBufferUAVDesc{}
				.SetName("VisibleInstanceBuffer")
				.SetStartingLen(1024u)
				.SetInitialState(nvrhi::ResourceStates::UnorderedAccess)
				.SetKeepInitialState(true));

		m_visibleInstanceCount.Init(
			device,
			StructuredBufferUAVDesc{}
				.SetName("VisibleInstanceCount")
				.SetStartingLen(1u)
				.SetInitialState(nvrhi::ResourceStates::UnorderedAccess)
				.SetKeepInitialState(true));

		auto frameConstantsDesc = DynamicConstantBufferDesc{};
		frameConstantsDesc.AddElement("viewMatrix", ElementType::kFloat4x4)
			.AddElement("projMatrix", ElementType::kFloat4x4)
			.AddElement("instanceCount", ElementType::kUInt)
			.SetName("FrameConstantBuffer");

		m_frameConstants = std::move(DynamicConstantBuffer{ device, frameConstantsDesc });

		m_cmdList = device->createCommandList();

		auto cullingShaderByteCode = core::file::readFileBytes("shaders/CS_CullAndCompact.cso"sv);
		m_cullingCS                = device->createShader(
            nvrhi::ShaderDesc{}
                .setShaderType(nvrhi::ShaderType::Compute)
                .setDebugName("CullingComputeShader"),
            cullingShaderByteCode.data(),
            cullingShaderByteCode.size());

		auto buildArgsCSByteCode = core::file::readFileBytes("shaders/CS_BuildDrawArgs.cso"sv);

		m_buildArgsCS = device->createShader(
			nvrhi::ShaderDesc{}
				.setShaderType(nvrhi::ShaderType::Compute)
				.setDebugName("BuildDrawArgsCS"),
			buildArgsCSByteCode.data(),
			buildArgsCSByteCode.size());

		namespace CB  = BindingSlots::CB;
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		auto bindingLayoutItem = m_frameConstants.GetBindingLayoutItem(CB::FrameConstants);
		m_cameraBindingSetItem = m_frameConstants.GetBindingSetItem(CB::FrameConstants);

		auto bindingLayoutDesc = nvrhi::BindingLayoutDesc{};
		bindingLayoutDesc.setRegisterSpace(BindingSpaces::PerFrameSpace)
			.addItem(bindingLayoutItem)
			.addItem(m_drawIndirectArgsBuffer.GetBindingLayoutItem(UAV::DrawIndirectArgs))
			.addItem(m_drawIndirectCountBuffer.GetBindingLayoutItem(UAV::DrawIndirectCount))
			.addItem(m_visibleInstanceBuffer.GetBindingLayoutItem(UAV::VisibleInstances))
			.addItem(m_visibleInstanceCount.GetBindingLayoutItem(UAV::VisibleInstanceCount))
			.addItem(registry.GetInstances().GetBindingLayoutItem(SRV::InstanceBuffer))
			.setVisibility(nvrhi::ShaderType::AllGraphics);

		registry.AttachBindingLayoutItems(bindingLayoutDesc);

		m_bindingLayout = device->createBindingLayout(bindingLayoutDesc);

		CreateBindingSet(registry, device);

		auto pipelineDesc = nvrhi::ComputePipelineDesc{};
		pipelineDesc.setComputeShader(m_cullingCS);
		pipelineDesc.addBindingLayout(m_bindingLayout);
		m_computePipeline = device->createComputePipeline(pipelineDesc);

		nvrhi::ComputePipelineDesc buildArgsPipelineDesc{};
		buildArgsPipelineDesc.setComputeShader(m_buildArgsCS);
		buildArgsPipelineDesc.addBindingLayout(m_bindingLayout);
		m_buildArgsPipeline = device->createComputePipeline(buildArgsPipelineDesc);
	}

	void
	CullingPass::AttachToFrameGraph(
		MeshRegistry&         registry,
		uint32_t              screenW,
		uint32_t              screenH,
		FrameGraph&           frameGraph,
		FrameGraphBlackboard& blackBoard,
		nvrhi::DeviceHandle   device,
		Camera&               camera)
	{
		blackBoard.add<FrameData>() = frameGraph.addCallbackPass<FrameData>(
			"CullingPass",
			[this, device, &registry](FrameGraph::Builder& builder, FrameData& data) {
				{
					data.frameConstants = builder.create<FrameGraphView<DynamicConstantBuffer>>(
						"FrameConstantsBuffer"sv,
						{ m_frameConstants });

					data.frameConstants = builder.write(data.frameConstants);
				}

				{
					data.drawIndirectArgs =
						builder.create<decltype(m_drawIndirectArgsBuffer)::View>(
							"Draw Indirect Args"sv,
							{ m_drawIndirectArgsBuffer });

					data.drawIndirectArgs = builder.write(data.drawIndirectArgs);
				}

				{
					data.drawIndirectCount =
						builder.create<decltype(m_drawIndirectCountBuffer)::View>(
							"Draw Indirect Count"sv,
							{ m_drawIndirectCountBuffer });

					data.drawIndirectCount = builder.write(data.drawIndirectCount);
				}

				{
					data.visibleInstanceCount =
						builder.create<decltype(m_visibleInstanceCount)::View>(
							"Visible Instance Count"sv,
							{ m_visibleInstanceCount });

					data.visibleInstanceCount = builder.write(data.visibleInstanceCount);
				}

				{
					data.visibleInstanceBuffer =
						builder.create<decltype(m_visibleInstanceBuffer)::View>(
							"Visible Instance Buffer"sv,
							{ m_visibleInstanceBuffer });

					data.visibleInstanceBuffer = builder.write(data.visibleInstanceBuffer);
				}

				{
					data.frameConstantsBindingSet =
						builder.create<FrameGraphView<nvrhi::BindingSetHandle>>(
							"Frame Constants Binding Set"sv,
							{ m_bindingSet });

					data.frameConstantsBindingSet = builder.write(data.frameConstantsBindingSet);
				}

				{
					data.frameConstantsBindingLayout =
						builder.create<FrameGraphView<nvrhi::BindingLayoutHandle>>(
							"Frame Constants Binding Layout"sv,
							{ m_bindingLayout });

					data.frameConstantsBindingLayout =
						builder.write(data.frameConstantsBindingLayout);
				}

				{
					data.vertexBuffer = builder.create<StructuredBufferSRV<Vertex>::View>(
						"Vertex Buffer"sv,
						{ registry.GetVertices() });

					data.vertexBuffer = builder.write(data.vertexBuffer);
				}

				{
					data.indexBuffer = builder.create<StructuredBufferSRV<uint32_t>::View>(
						"Index Buffer"sv,
						{ registry.GetIndices() });

					data.indexBuffer = builder.write(data.indexBuffer);
				}
			},
			[=, &registry](const FrameData& data, FrameGraphPassResources& resources, void* ctx) {
				if (camera.ShouldUpdate())
				{
					auto gfx                       = static_cast<IGraphics*>(ctx);
					auto device                    = gfx->GetDevice();
					m_frameConstants["viewMatrix"] = camera.GetViewMatrix();
					m_frameConstants["projMatrix"] = camera.GetProjMatrix();
				}

				auto instanceCount                = registry.GetInstancesCount();
				m_frameConstants["instanceCount"] = instanceCount;

				m_cmdList->open();

				// allocated a new buffer, must recreate binding set
				if (registry.Update(m_cmdList, device))
				{
					CreateBindingSet(registry, device);
				}
				auto constexpr resetCounter = std::array<uint32_t, 1>{ 0 };

				m_cmdList->beginTrackingBufferState(
					m_visibleInstanceBuffer.GetBuffer(),
					nvrhi::ResourceStates::UnorderedAccess);

				m_cmdList->beginTrackingBufferState(
					m_visibleInstanceCount.GetBuffer(),
					nvrhi::ResourceStates::UnorderedAccess);

				m_cmdList->beginTrackingBufferState(
					m_drawIndirectArgsBuffer.GetBuffer(),
					nvrhi::ResourceStates::UnorderedAccess);

				m_visibleInstanceCount.Update(m_cmdList, resetCounter);
				m_frameConstants.Update(m_cmdList);

				auto computeState = nvrhi::ComputeState{};
				computeState.setPipeline(m_computePipeline).addBindingSet(m_bindingSet);
				m_cmdList->setComputeState(computeState);

				auto buildArgsState = nvrhi::ComputeState{};
				buildArgsState.setPipeline(m_buildArgsPipeline);
				buildArgsState.addBindingSet(m_bindingSet);

				m_cmdList->setBufferState(
					m_drawIndirectArgsBuffer.GetBuffer(),
					nvrhi::ResourceStates::UnorderedAccess);

				const auto threadsPerGroup = 64u;
				const auto numGroups = (instanceCount + threadsPerGroup - 1) / threadsPerGroup;
				m_cmdList->dispatch(numGroups, 1, 1);

				m_cmdList->setComputeState(buildArgsState);
				m_cmdList->dispatch(1, 1, 1);

				m_cmdList->close();
				device->executeCommandList(m_cmdList);
			});
	}

	void
	CullingPass::CreateBindingSet(MeshRegistry& registry, nvrhi::DeviceHandle device)
	{
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		auto bindingSetDesc = nvrhi::BindingSetDesc{};
		bindingSetDesc.addItem(m_cameraBindingSetItem)
			.addItem(m_drawIndirectArgsBuffer.GetBindingSetItem(UAV::DrawIndirectArgs))
			.addItem(m_drawIndirectCountBuffer.GetBindingSetItem(UAV::DrawIndirectCount))
			.addItem(m_visibleInstanceBuffer.GetBindingSetItem(UAV::VisibleInstances))
			.addItem(m_visibleInstanceCount.GetBindingSetItem(UAV::VisibleInstanceCount))
			.addItem(registry.GetInstances().GetBindingSetItem(SRV::InstanceBuffer));

		registry.AttachBindingSetItems(bindingSetDesc);

		m_bindingSet = device->createBindingSet(bindingSetDesc, m_bindingLayout);
	}

}
