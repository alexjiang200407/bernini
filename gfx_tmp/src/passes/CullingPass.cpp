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
			StructuredBufferGPUDesc{}
				.SetName("DrawIndirectArgs")
				.SetStartingLen(1024)
				.SetIsDrawIndirect()
				.SetInitialState(nvrhi::ResourceStates::IndirectArgument));

		m_drawIndirectCountBuffer.Init(
			device,
			StructuredBufferGPUDesc{}
				.SetName("DrawIndirectCount")
				.SetStartingLen(1u)
				.SetInitialState(nvrhi::ResourceStates::UnorderedAccess)
				.SetKeepInitialState(true));

		m_meshVisibleCountBuffer.Init(
			device,
			StructuredBufferGPUDesc{}
				.SetName("MeshVisibleCounts")
				.SetStartingLen(1024)
				.SetInitialState(nvrhi::ResourceStates::UnorderedAccess)
				.SetKeepInitialState(true));

		m_meshInstanceOffsetBuffer.Init(
			device,
			StructuredBufferGPUDesc{}
				.SetName("MeshInstanceOffsets")
				.SetStartingLen(1024)
				.SetInitialState(nvrhi::ResourceStates::UnorderedAccess)
				.SetKeepInitialState(true));

		m_meshWriteCursor.Init(
			device,
			StructuredBufferGPUDesc{}
				.SetName("MeshWriteCursor")
				.SetStartingLen(1024)
				.SetInitialState(nvrhi::ResourceStates::UnorderedAccess)
				.SetKeepInitialState(true));

		m_compactedInstanceBuffer.Init(
			device,
			StructuredBufferGPUDesc{}
				.SetName("CompactedInstances")
				.SetStartingLen(1024)
				.SetInitialState(nvrhi::ResourceStates::UnorderedAccess)
				.SetKeepInitialState(true));

		m_drawIndirectArgsBuffer.Init(
			device,
			StructuredBufferGPUDesc{}
				.SetName("DrawIndirectArgs")
				.SetStartingLen(1024)
				.SetIsDrawIndirect()
				.SetInitialState(nvrhi::ResourceStates::IndirectArgument)
				.SetKeepInitialState(true));

		auto frameConstantsDesc = DynamicConstantBufferDesc{};
		frameConstantsDesc.AddElement("viewMatrix", ElementType::kFloat4x4)
			.AddElement("projMatrix", ElementType::kFloat4x4)
			.AddElement("instanceCount", ElementType::kUInt)
			.AddElement("meshCount", ElementType::kUInt)
			.SetName("FrameConstantBuffer");

		m_frameConstants = std::move(DynamicConstantBuffer{ device, frameConstantsDesc });

		m_cmdList = device->createCommandList();

		auto histogramShaderByteCode = core::file::readFileBytes("shaders/CS_CullHistogram.cso"sv);
		m_cullHistogramCS            = device->createShader(
            nvrhi::ShaderDesc{}
                .setShaderType(nvrhi::ShaderType::Compute)
                .setDebugName("CullingComputeShader"),
            histogramShaderByteCode.data(),
            histogramShaderByteCode.size());

		auto prefixSumShaderByteCode = core::file::readFileBytes("shaders/CS_PrefixSum.cso"sv);
		m_prefixSumCS                = device->createShader(
            nvrhi::ShaderDesc{}
                .setShaderType(nvrhi::ShaderType::Compute)
                .setDebugName("PrefixSumComputeShader"),
            prefixSumShaderByteCode.data(),
            prefixSumShaderByteCode.size());

		auto scatterShaderByteCode = core::file::readFileBytes("shaders/CS_CullScatter.cso"sv);
		m_cullScatterCS            = device->createShader(
            nvrhi::ShaderDesc{}
                .setShaderType(nvrhi::ShaderType::Compute)
                .setDebugName("ScatterComputeShader"),
            scatterShaderByteCode.data(),
            scatterShaderByteCode.size());

		auto buildArgsCSByteCode = core::file::readFileBytes("shaders/CS_BuildDrawArgs.cso"sv);
		m_buildArgsCS            = device->createShader(
            nvrhi::ShaderDesc{}
                .setShaderType(nvrhi::ShaderType::Compute)
                .setDebugName("BuildDrawArgsCS"),
            buildArgsCSByteCode.data(),
            buildArgsCSByteCode.size());

		namespace CB  = BindingSlots::CB;
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		auto frameConstantsBindingLayout =
			m_frameConstants.GetBindingLayoutItem(CB::FrameConstants);
		m_frameConstantsBindingSetItem = m_frameConstants.GetBindingSetItem(CB::FrameConstants);
		// Compute draw args binding layout
		{
			auto bindingLayoutDesc = nvrhi::BindingLayoutDesc{};
			bindingLayoutDesc.setRegisterSpace(BindingSpaces::PerFrameSpace)
				.addItem(frameConstantsBindingLayout)
				.addItem(m_drawIndirectArgsBuffer.GetBindingLayoutItemUAV(UAV::DrawIndirectArgs))
				.addItem(m_drawIndirectCountBuffer.GetBindingLayoutItemUAV(UAV::DrawIndirectCount))
				.addItem(m_meshVisibleCountBuffer.GetBindingLayoutItemUAV(UAV::MeshVisibleCounts))
				.addItem(
					m_meshInstanceOffsetBuffer.GetBindingLayoutItemUAV(UAV::MeshInstanceOffsets))
				.addItem(m_meshWriteCursor.GetBindingLayoutItemUAV(UAV::MeshWriteCursor))
				.addItem(m_compactedInstanceBuffer.GetBindingLayoutItemUAV(UAV::CompactedInstances))
				.setVisibility(nvrhi::ShaderType::Compute);

			registry.AttachBindingLayoutItems(bindingLayoutDesc);

			m_computeBindingLayout = device->createBindingLayout(bindingLayoutDesc);
		}

		// Draw binding layout
		{
			auto bindingLayoutDesc = nvrhi::BindingLayoutDesc{};
			bindingLayoutDesc.setRegisterSpace(BindingSpaces::PerFrameSpace)
				.addItem(frameConstantsBindingLayout)
				.addItem(m_compactedInstanceBuffer.GetBindingLayoutItemSRV(SRV::CompactedInstances))
				.setVisibility(nvrhi::ShaderType::AllGraphics);

			registry.AttachBindingLayoutItems(bindingLayoutDesc);

			m_drawBindingLayout = device->createBindingLayout(bindingLayoutDesc);
		}

		CreateBindingSet(registry, device);

		auto makePipeline = [&](nvrhi::ShaderHandle cs) {
			nvrhi::ComputePipelineDesc pd;
			pd.setComputeShader(cs);
			pd.addBindingLayout(m_computeBindingLayout);
			return device->createComputePipeline(pd);
		};

		m_histogramPipeline = makePipeline(m_cullHistogramCS);
		m_prefixSumPipeline = makePipeline(m_prefixSumCS);
		m_scatterPipeline   = makePipeline(m_cullScatterCS);
		m_buildArgsPipeline = makePipeline(m_buildArgsCS);
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
					data.meshVisibleCounts =
						builder.create<decltype(m_meshVisibleCountBuffer)::View>(
							"MeshVisibleCounts",
							{ m_meshVisibleCountBuffer });

					data.meshVisibleCounts = builder.write(data.meshVisibleCounts);
				}

				{
					data.meshInstanceOffsets =
						builder.create<decltype(m_meshInstanceOffsetBuffer)::View>(
							"MeshInstanceOffsets",
							{ m_meshInstanceOffsetBuffer });
					data.meshInstanceOffsets = builder.write(data.meshInstanceOffsets);
				}

				{
					data.meshWriteCursor = builder.create<decltype(m_meshWriteCursor)::View>(
						"MeshWriteCursor",
						{ m_meshWriteCursor });
					data.meshWriteCursor = builder.write(data.meshWriteCursor);
				}

				{
					data.compactedInstances =
						builder.create<decltype(m_compactedInstanceBuffer)::View>(
							"CompactedInstanceBuffer"sv,
							{ m_compactedInstanceBuffer });
					data.compactedInstances = builder.write(data.compactedInstances);
				}

				{
					data.frameConstantsBindingSet =
						builder.create<FrameGraphView<nvrhi::BindingSetHandle>>(
							"Frame Constants Binding Set"sv,
							{ m_drawBindingSet });

					data.frameConstantsBindingSet = builder.write(data.frameConstantsBindingSet);
				}

				{
					data.frameConstantsBindingLayout =
						builder.create<FrameGraphView<nvrhi::BindingLayoutHandle>>(
							"Frame Constants Binding Layout"sv,
							{ m_drawBindingLayout });

					data.frameConstantsBindingLayout =
						builder.write(data.frameConstantsBindingLayout);
				}

				{
					data.vertexBuffer = builder.create<StructuredUploadBuffer<Vertex>::View>(
						"Vertex Buffer"sv,
						{ registry.GetVertices() });

					data.vertexBuffer = builder.write(data.vertexBuffer);
				}

				{
					data.indexBuffer = builder.create<StructuredUploadBuffer<uint32_t>::View>(
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
				auto meshCount                = static_cast<uint32_t>(registry.GetMeshInfosCount());
				m_frameConstants["meshCount"] = meshCount;

				m_cmdList->open();

				// allocated a new buffer, must recreate binding set
				if (registry.Update(m_cmdList, device))
				{
					CreateBindingSet(registry, device);
				}
				auto constexpr resetCounter = std::array<uint32_t, 1>{ 0 };

				m_meshVisibleCountBuffer.Update(m_cmdList, resetCounter);
				m_frameConstants.Update(m_cmdList);

				const auto threadsPerGroup = 64u;
				const auto numGroups = (instanceCount + threadsPerGroup - 1) / threadsPerGroup;

				{
					nvrhi::ComputeState cs;
					cs.setPipeline(m_histogramPipeline).addBindingSet(m_computeBindingSet);
					m_cmdList->setComputeState(cs);
					m_cmdList->dispatch(numGroups, 1, 1);
				}

				{
					nvrhi::ComputeState cs;
					cs.setPipeline(m_prefixSumPipeline).addBindingSet(m_computeBindingSet);
					m_cmdList->setComputeState(cs);
					m_cmdList->dispatch(1, 1, 1);
				}

				m_cmdList->commitBarriers();

				{
					nvrhi::ComputeState cs;
					cs.setPipeline(m_scatterPipeline).addBindingSet(m_computeBindingSet);
					m_cmdList->setComputeState(cs);
					m_cmdList->dispatch(numGroups, 1, 1);
				}

				{
					nvrhi::ComputeState cs;
					cs.setPipeline(m_buildArgsPipeline).addBindingSet(m_computeBindingSet);
					m_cmdList->setComputeState(cs);
					m_cmdList->dispatch(1, 1, 1);
				}

				m_cmdList->setBufferState(
					m_drawIndirectArgsBuffer.GetBuffer(),
					nvrhi::ResourceStates::IndirectArgument);

				m_cmdList->setBufferState(
					m_drawIndirectCountBuffer.GetBuffer(),
					nvrhi::ResourceStates::IndirectArgument);

				m_cmdList->setBufferState(
					m_compactedInstanceBuffer.GetBuffer(),
					nvrhi::ResourceStates::ShaderResource);

				m_cmdList->commitBarriers();

				m_cmdList->close();
				device->executeCommandList(m_cmdList);
			});
	}

	void
	CullingPass::CreateBindingSet(MeshRegistry& registry, nvrhi::DeviceHandle device)
	{
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		{
			auto bindingSetDesc = nvrhi::BindingSetDesc{};
			bindingSetDesc.addItem(m_frameConstantsBindingSetItem)
				.addItem(m_drawIndirectArgsBuffer.GetBindingSetItemUAV(UAV::DrawIndirectArgs))
				.addItem(m_drawIndirectCountBuffer.GetBindingSetItemUAV(UAV::DrawIndirectCount))
				.addItem(m_meshVisibleCountBuffer.GetBindingSetItemUAV(UAV::MeshVisibleCounts))
				.addItem(m_meshInstanceOffsetBuffer.GetBindingSetItemUAV(UAV::MeshInstanceOffsets))
				.addItem(m_meshWriteCursor.GetBindingSetItemUAV(UAV::MeshWriteCursor))
				.addItem(m_compactedInstanceBuffer.GetBindingSetItemUAV(UAV::CompactedInstances));

			registry.AttachBindingSetItems(bindingSetDesc);

			m_computeBindingSet = device->createBindingSet(bindingSetDesc, m_computeBindingLayout);
		}

		{
			auto bindingSetDesc = nvrhi::BindingSetDesc{};
			bindingSetDesc.addItem(m_frameConstantsBindingSetItem)
				.addItem(m_compactedInstanceBuffer.GetBindingSetItemSRV(SRV::CompactedInstances));

			registry.AttachBindingSetItems(bindingSetDesc);

			m_drawBindingSet = device->createBindingSet(bindingSetDesc, m_drawBindingLayout);
		}
	}

}
