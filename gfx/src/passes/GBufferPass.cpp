#include "passes/GBufferPass.h"
#include "BindingSlots.h"
#include "camera/Camera.h"
#include "frame_graph/FrameGraphView.h"
#include "mesh/Mesh.h"
#include "mesh/MeshRegistry.h"
#include "mesh/Vertex.h"
#include <core/file/file.h>
#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace gfx
{
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

	void
	GBufferPass::Init(nvrhi::DeviceHandle device, MeshRegistry& registry)
	{
		m_mainCommandList = device->createCommandList();

		{
			auto meshShaderBytecode = core::file::readFileBytes("shaders/MS_GBuffer_v2.cso"sv);
			m_meshShader            = device->createShader(
                nvrhi::ShaderDesc{}
                    .setShaderType(nvrhi::ShaderType::Mesh)
                    .setDebugName("GBuffer Mesh Shader"),
                meshShaderBytecode.data(),
                meshShaderBytecode.size());
		}

		{
			auto pixelShaderBytecode = core::file::readFileBytes("shaders/PS_GBuffer.cso"sv);
			m_pixelShader            = device->createShader(
                nvrhi::ShaderDesc{}
                    .setShaderType(nvrhi::ShaderType::Pixel)
                    .setDebugName("GBuffer Pixel Shader"),
                pixelShaderBytecode.data(),
                pixelShaderBytecode.size());
		}

		{
			auto bindingLayoutDesc = nvrhi::BindingLayoutDesc{};
			bindingLayoutDesc.setRegisterSpace(BindingSpaces::PerFrameSpace)
				.addItem(m_frameConstants.GetBindingLayoutItem(BindingSlots::CB::FrameConstants))
				.setVisibility(nvrhi::ShaderType::All);

			registry.AttachBindingLayoutItems(bindingLayoutDesc);

			m_perFrameBindingLayout = device->createBindingLayout(bindingLayoutDesc);
		}

		{
			auto desc = ComputeBufferDesc{};
			desc.SetElement<MeshletIndirectDrawArg>().SetElementCount(1).SetName(
				"IndirectDrawArgument");
			m_indirectDrawArguments.Init(device, desc);
		}

		{
			auto desc = ComputeBufferDesc{};
			desc.SetElement<uint32_t>().SetElementCount(1).SetName("VisibleMeshletIndices");
			m_visibleMeshletIndices.Init(device, desc);
		}

		{
			auto bindingLayoutDesc = nvrhi::BindingLayoutDesc{};
			bindingLayoutDesc.setRegisterSpace(BindingSpaces::IndirectDrawData)
				.addItem(m_indirectDrawPushConstants.GetBindingLayoutItem(
					BindingSlots::CB::IndirectPushConstants))
				.addItem(
					m_indirectDrawArguments.GetBindingLayoutItem(BindingSlots::UAV::DrawArgsBuffer))
				.addItem(m_visibleMeshletIndices.GetBindingLayoutItem(
					BindingSlots::UAV::VisibleMeshletIndices))
				.setVisibility(nvrhi::ShaderType::All);

			m_indirectDrawDataBindingLayout = device->createBindingLayout(bindingLayoutDesc);
		}

		{
			auto frameConstantsDesc = DynamicConstantBufferDesc{};
			frameConstantsDesc.AddElement("viewMatrix", ElementType::kFloat4x4)
				.AddElement("projMatrix", ElementType::kFloat4x4)
				.SetName("FrameConstantBuffer");

			m_frameConstants.Init(device, frameConstantsDesc);
		}

		{
			auto indirectDrawConstantsDesc = DynamicConstantBufferDesc{};
			indirectDrawConstantsDesc.AddElement("drawIndex", ElementType::kUInt)
				.SetName("IndirectDrawConstats")
				.SetIsPushConstant();

			m_indirectDrawPushConstants.Init(device, indirectDrawConstantsDesc);
		}
	}

	void
	GBufferPass::CreateBindingSet(MeshRegistry& registry, nvrhi::DeviceHandle device)
	{
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		{
			auto bindingSetDesc = nvrhi::BindingSetDesc{};
			bindingSetDesc.addItem(
				m_frameConstants.GetBindingSetItem(BindingSlots::CB::FrameConstants));
			registry.AttachBindingSetItems(bindingSetDesc);

			m_perFrameBindingSet =
				device->createBindingSet(bindingSetDesc, m_perFrameBindingLayout);
		}

		{
			auto bindingSetDesc = nvrhi::BindingSetDesc{};
			bindingSetDesc
				.addItem(m_indirectDrawPushConstants.GetBindingSetItem(
					BindingSlots::CB::IndirectPushConstants))
				.addItem(
					m_indirectDrawArguments.GetBindingSetItem(BindingSlots::UAV::DrawArgsBuffer))
				.addItem(m_visibleMeshletIndices.GetBindingSetItem(
					BindingSlots::UAV::VisibleMeshletIndices));

			m_indirectDrawDataBindingSet =
				device->createBindingSet(bindingSetDesc, m_indirectDrawDataBindingLayout);
		}
	}

	bool
	GBufferPass::Update(uint32_t meshletInstances)
	{
		if (meshletInstances > m_visibleMeshletIndices.Size())
		{
			m_visibleMeshletIndices.Resize(meshletInstances * 2);
			return true;
		}

		return false;
	}

	void
	GBufferPass::AttachToFrameGraph(
		FrameGraph&           frameGraph,
		FrameGraphBlackboard& blackBoard,
		MeshRegistry&         registry,
		Camera&               camera,
		RenderArgs            renderArgs)
	{
		frameGraph.addCallbackPass(
			"GBufferPass",
			[](FrameGraph::Builder& builder, auto&) { builder.setSideEffect(); },
			[this, renderArgs, &registry, &camera](const auto&, FrameGraphPassResources&, void*) {
				auto device            = renderArgs.device;
				auto screenW           = renderArgs.screenWidth;
				auto screenH           = renderArgs.screenHeight;
				auto outputFramebuffer = renderArgs.outBuffer;
				auto frameBufferInfo   = renderArgs.outBufferInfo;
				auto instanceCount     = registry.GetInstancesCount();

				m_mainCommandList->open();

				auto state = nvrhi::MeshletState{};

				// TODO: Could move this into init?
				if (!m_pipeline)
				{
					auto psoDesc = nvrhi::MeshletPipelineDesc{};

					psoDesc.setMeshShader(m_meshShader)
						.setPixelShader(m_pixelShader)
						.setPrimType(nvrhi::PrimitiveType::TriangleList)
						.setRenderState(renderState)
						.addBindingLayout(m_perFrameBindingLayout)
						.addBindingLayout(m_indirectDrawDataBindingLayout);

					m_pipeline = device->createMeshletPipeline(psoDesc, frameBufferInfo);
				}

				if (camera.ShouldUpdate())
				{
					m_frameConstants["viewMatrix"] = camera.GetViewMatrix();
					m_frameConstants["projMatrix"] = camera.GetProjMatrix();
				}

				m_frameConstants["instanceCount"] = instanceCount;

				if (registry.Update(m_mainCommandList, device) ||
			        Update(registry.GetInstancesCount()))
				{
					CreateBindingSet(registry, device);
				}

				m_frameConstants.Update(m_mainCommandList);

				state.setIndirectParams(m_indirectDrawArguments.GetBuffer())
					.addBindingSet(m_perFrameBindingSet)
					.addBindingSet(m_indirectDrawDataBindingSet)
					.setFramebuffer(outputFramebuffer)
					.setViewport(
						nvrhi::ViewportState{}.addViewportAndScissorRect(
							nvrhi::Viewport{ screenW, screenH }));

				for (uint32_t i = 0; i < 1; ++i)
				{
					m_indirectDrawPushConstants["drawIndex"] = i;
					m_indirectDrawPushConstants.Update(m_mainCommandList);

					state.setPipeline(m_pipeline);

					m_mainCommandList->setMeshletState(state);
					m_mainCommandList->dispatchIndirect(sizeof(MeshletIndirectDrawArg) * i);
				}

				m_mainCommandList->close();
				device->executeCommandList(m_mainCommandList);
			});
	}
}
