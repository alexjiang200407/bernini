#include "passes/GBufferPass.h"
#include "BindingSlots.h"
#include "passes/output/FrameData.h"
#include "passes/output/SortedInstancesData.h"
#include "shader_util/util.h"
#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace gfx
{
	void
	GBufferPass::Init(
		nvrhi::DeviceHandle        device,
		nvrhi::BindingLayoutHandle blPerFrame,
		nvrhi::BindingLayoutHandle blSortedInstances,
		nvrhi::FramebufferInfo     outBufferInfo)
	{
		m_blPerFrame        = blPerFrame;
		m_blSortedInstances = blSortedInstances;

		m_drawArgs = ComputeBufferDesc{}
		                 .SetElement<MeshletDispatchArg>()
		                 .SetElementCount(PSO_COUNT)
		                 .SetInitialState(nvrhi::ResourceStates::IndirectArgument)
		                 .SetName("GBufferPass Draw Args")
		                 .Create(device);

		m_cmdList = device->createCommandList();

		{
			auto rootConstDesc = DynamicConstantBufferDesc{};
			rootConstDesc.AddElement("binIndex", ElementType::kUInt)
				.SetIsPushConstant()
				.SetName("Bin Index");

			m_gBufferRootConstants.Init(device, rootConstDesc);

			auto bsDesc = nvrhi::BindingSetDesc{};
			auto blDesc = nvrhi::BindingLayoutDesc{};

			blDesc.addItem(nvrhi::BindingLayoutItem::PushConstants(0, 16))
				.setVisibility(nvrhi::ShaderType::Amplification)
				.setRegisterSpace(BindingSpaces::GBufferSpace);
			bsDesc.addItem(m_gBufferRootConstants.GetBindingSetItem(0));

			m_blDrawArgs = device->createBindingLayout(blDesc);
			m_bsDrawArgs = device->createBindingSet(bsDesc, m_blDrawArgs);
		}

		{
			auto blDesc = nvrhi::BindingLayoutDesc{};
			blDesc.setRegisterSpace(BindingSpaces::GBufferSpace)
				.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0))
				.setVisibility(nvrhi::ShaderType::All);

			auto blGenDispatchArgs = device->createBindingLayout(blDesc);

			auto bsDesc = nvrhi::BindingSetDesc{};
			bsDesc.addItem(m_drawArgs.GetBindingSetItemUAV(0));
			m_bsGenDispatchArgs = device->createBindingSet(bsDesc, blGenDispatchArgs);

			auto computePipelineDesc = nvrhi::ComputePipelineDesc{};
			computePipelineDesc.addBindingLayout(blGenDispatchArgs)
				.addBindingLayout(blSortedInstances)
				.setComputeShader(createShaderFromFile(
					device,
					"shaders/CS_GBuffer_GenDispatchArgs.cso",
					nvrhi::ShaderType::Compute,
					"CS_GBuffer_GenDispatchArgs"));

			m_genDispatchArgsPipeline = device->createComputePipeline(computePipelineDesc);
		}

		static auto renderState = nvrhi::RenderState{}
		                              .setRasterState(
										  nvrhi::RasterState{}
											  .setCullMode(nvrhi::RasterCullMode::None)
											  .setFillMode(nvrhi::RasterFillMode::Solid))
		                              .setDepthStencilState(
										  nvrhi::DepthStencilState{}
											  .setDepthTestEnable(true)
											  .setDepthWriteEnable(true)
											  .setDepthFunc(nvrhi::ComparisonFunc::Less)
											  .setStencilEnable(false));

		AddDrawStrategy(
			device,
			PSO::kOpaque_StaticMesh_PBR,
			outBufferInfo,
			"shaders/AS_GBuffer_StaticMesh.cso",
			"shaders/MS_GBuffer_StaticMesh.cso",
			"shaders/PS_GBuffer_Green.cso",
			renderState,
			m_blDrawArgs);

		AddDrawStrategy(
			device,
			PSO::kAlphaTest_StaticMesh_PBR,
			outBufferInfo,
			"shaders/AS_GBuffer_StaticMesh.cso",
			"shaders/MS_GBuffer_StaticMesh.cso",
			"shaders/PS_GBuffer_Green.cso",
			renderState,
			m_blDrawArgs);

		AddDrawStrategy(
			device,
			PSO::kTransparent_StaticMesh_PBR,
			outBufferInfo,
			"shaders/AS_GBuffer_StaticMesh.cso",
			"shaders/MS_GBuffer_StaticMesh.cso",
			"shaders/PS_GBuffer_Green.cso",
			renderState,
			m_blDrawArgs);
	}

	void
	GBufferPass::BuildDrawArgs(BindingSetView bsSortData)
	{
		auto state = nvrhi::ComputeState{};
		state.setPipeline(m_genDispatchArgsPipeline).addBindingSet(m_bsGenDispatchArgs);
		bsSortData.AttachBindingSetTo(state);

		m_cmdList->setComputeState(state);
		m_cmdList->dispatch(PSO_COUNT);
	}

	void
	GBufferPass::DispatchMeshlets(
		BindingSetView           bsFrameData,
		BindingSetView           bsSortedData,
		nvrhi::FramebufferHandle frameBuffer,
		nvrhi::ViewportState     vpState)
	{
		for (auto x = 0; x < PSO_COUNT; ++x)
		{
			auto state = nvrhi::MeshletState{};
			state.setIndirectParams(m_drawArgs.GetBuffer())
				.setPipeline(m_meshletPipeline[x])
				.setFramebuffer(frameBuffer)
				.setViewport(vpState);

			bsFrameData.AttachBindingSetTo(state);
			bsSortedData.AttachBindingSetTo(state);
			state.addBindingSet(m_bsDrawArgs);

			m_cmdList->setMeshletState(state);

			m_gBufferRootConstants["binIndex"] = x;
			m_gBufferRootConstants.Update(m_cmdList);

			m_cmdList->dispatchMeshIndirect(x * sizeof(MeshletDispatchArg));
		}
	}

	void
	GBufferPass::SetPipeline(PSO pso, nvrhi::MeshletPipelineHandle pipeline)
	{
		m_meshletPipeline[static_cast<uint32_t>(pso)] = pipeline;
	}

	void
	GBufferPass::AttachToFrameGraph(
		FrameGraph&              frameGraph,
		FrameGraphBlackboard&    blackBoard,
		nvrhi::DeviceHandle      device,
		nvrhi::FramebufferHandle frameBuffer,
		uint32_t                 screenWidth,
		uint32_t                 screenHeight)
	{
		auto frameData        = blackBoard.get<FrameData>();
		auto sortInstanceData = blackBoard.get<SortedInstancesData>();

		frameGraph.addCallbackPass(
			"GBufferPass",
			[this, device, frameData, sortInstanceData](FrameGraph::Builder& builder, auto& data) {
				builder.read(frameData.bsFrameData);
				builder.read(frameData.instanceCount);
				builder.read(sortInstanceData.bsSortedInstances);

				builder.setSideEffect();
			},
			[=, &blackBoard](const auto&, FrameGraphPassResources& res, void*) {
				m_cmdList->open();

				auto  instanceCount = res.get<FGCount>(frameData.instanceCount).Get();
				auto& bsSortedInstances =
					res.get<FGBindingSet>(sortInstanceData.bsSortedInstances).Get();
				auto& bsFrameData = res.get<FGBindingSet>(frameData.bsFrameData).Get();

				bsFrameData.TrackResources(m_cmdList);
				bsSortedInstances.TrackResources(m_cmdList);

				m_drawArgs.TrackResourceState(m_cmdList, nvrhi::ResourceStates::IndirectArgument);

				m_drawArgs.TransitionState(m_cmdList, nvrhi::ResourceStates::UnorderedAccess);

				BuildDrawArgs(bsSortedInstances);

				m_drawArgs.TransitionState(m_cmdList, nvrhi::ResourceStates::IndirectArgument);

				auto vp = nvrhi::Viewport{ static_cast<float>(screenWidth),
				                           static_cast<float>(screenHeight) };
				DispatchMeshlets(
					bsFrameData,
					bsSortedInstances,
					frameBuffer,
					nvrhi::ViewportState{}.addViewportAndScissorRect(vp));

				m_cmdList->close();

				device->executeCommandList(m_cmdList);
			});
	}

	void
	GBufferPass::AddDrawStrategy(
		nvrhi::DeviceHandle        device,
		PSO                        pso,
		nvrhi::FramebufferInfo     outBufferInfo,
		const std::string&         ampShader,
		const std::string&         meshShader,
		const std::string&         pixShader,
		nvrhi::RenderState         renderState,
		nvrhi::BindingLayoutHandle blDrawArgs)
	{
		auto desc = nvrhi::MeshletPipelineDesc{};
		desc.addBindingLayout(m_blPerFrame)
			.addBindingLayout(m_blSortedInstances)
			.addBindingLayout(blDrawArgs)
			.setPrimType(nvrhi::PrimitiveType::TriangleList)
			.setRenderState(renderState)
			.setAmplificationShader(createShaderFromFile(
				device,
				ampShader,
				nvrhi::ShaderType::Amplification,
				"AS_GBuffer"))
			.setMeshShader(
				createShaderFromFile(device, meshShader, nvrhi::ShaderType::Mesh, "MS_GBuffer"))
			.setPixelShader(
				createShaderFromFile(device, pixShader, nvrhi::ShaderType::Pixel, "PS_GBuffer"));

		auto idx               = static_cast<uint32_t>(pso);
		m_meshletPipeline[idx] = device->createMeshletPipeline(desc, outBufferInfo);
	}
}
