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
		m_drawIndirectBuffer.Init(
			device,
			StructuredBufferUAVDesc{}
				.SetName("DrawIndirectBuffer")
				.SetStartingLen(1024u)
				.SetIsDrawIndirect()
				.SetInitialState(nvrhi::ResourceStates::IndirectArgument));

		m_drawIndirectBufferCounter.Init(
			device,
			StructuredBufferUAVDesc{}
				.SetName("DrawIndirectBufferCounter")
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

		auto cullingShaderByteCode = core::file::readFileBytes("shaders/CS_DrawIndirect.cso"sv);
		m_cullingCS                = device->createShader(
            nvrhi::ShaderDesc{}
                .setShaderType(nvrhi::ShaderType::Compute)
                .setDebugName("CullingComputeShader"),
            cullingShaderByteCode.data(),
            cullingShaderByteCode.size());

		namespace CB  = BindingSlots::CB;
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		auto bindingLayoutItem = m_frameConstants.GetBindingLayoutItem(CB::FrameConstants);
		m_cameraBindingSetItem = m_frameConstants.GetBindingSetItem(CB::FrameConstants);

		auto bindingLayoutDesc = nvrhi::BindingLayoutDesc{};
		bindingLayoutDesc.setRegisterSpace(BindingSpaces::PerFrameSpace)
			.addItem(bindingLayoutItem)
			.addItem(m_drawIndirectBuffer.GetBindingLayoutItem(UAV::DrawIndirectArg))
			.addItem(m_drawIndirectBufferCounter.GetBindingLayoutItem(UAV::DrawIndirectArgCount))
			.setVisibility(nvrhi::ShaderType::AllGraphics);

		registry.AttachBindingLayoutItems(bindingLayoutDesc);

		m_bindingLayout = device->createBindingLayout(bindingLayoutDesc);

		CreateBindingSet(registry, device);

		auto pipelineDesc = nvrhi::ComputePipelineDesc{};
		pipelineDesc.setComputeShader(m_cullingCS);
		pipelineDesc.addBindingLayout(m_bindingLayout);
		m_computePipeline = device->createComputePipeline(pipelineDesc);
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
					data.drawIndirectBuffer = builder.create<decltype(m_drawIndirectBuffer)::View>(
						"Draw Indirect Buffer"sv,
						{ m_drawIndirectBuffer });

					data.drawIndirectBuffer = builder.write(data.drawIndirectBuffer);
				}

				{
					data.drawIndirectCountBuffer =
						builder.create<decltype(m_drawIndirectBufferCounter)::View>(
							"Draw Indirect Count"sv,
							{ m_drawIndirectBufferCounter });

					data.drawIndirectCountBuffer = builder.write(data.drawIndirectCountBuffer);
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
					m_drawIndirectBuffer.GetBuffer(),
					nvrhi::ResourceStates::UnorderedAccess);

				m_drawIndirectBufferCounter.Update(m_cmdList, resetCounter);
				m_frameConstants.Update(m_cmdList);

				auto computeState = nvrhi::ComputeState{};
				computeState.setPipeline(m_computePipeline).addBindingSet(m_bindingSet);
				m_cmdList->setComputeState(computeState);

				const auto threadsPerGroup = 64u;
				const auto numGroups = (instanceCount + threadsPerGroup - 1) / threadsPerGroup;
				m_cmdList->dispatch(numGroups, 1, 1);

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
			.addItem(m_drawIndirectBuffer.GetBindingSetItem(UAV::DrawIndirectArg))
			.addItem(m_drawIndirectBufferCounter.GetBindingSetItem(UAV::DrawIndirectArgCount));

		registry.AttachBindingSetItems(bindingSetDesc);

		m_bindingSet = device->createBindingSet(bindingSetDesc, m_bindingLayout);
	}

}
