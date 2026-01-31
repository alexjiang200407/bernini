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
	static auto renderState =
		nvrhi::RenderState{}
			.setRasterState(nvrhi::RasterState{}
	                            .setCullMode(nvrhi::RasterCullMode::None)
	                            .setFillMode(nvrhi::RasterFillMode::Solid))
			.setDepthStencilState(nvrhi::DepthStencilState{}
	                                  .setDepthTestEnable(true)
	                                  .setDepthWriteEnable(true)
	                                  .setDepthFunc(nvrhi::ComparisonFunc::Less)
	                                  .setStencilEnable(false));
	void
	GBufferPass::Init(nvrhi::DeviceHandle device, MeshRegistry& registry)
	{
		m_mainCommandList = device->createCommandList();

		{
			auto meshShaderBytecode = core::file::readFileBytes("shaders/MS_GBuffer.cso"sv);
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
			auto ampShaderBytecode = core::file::readFileBytes("shaders/AS_GBuffer.cso"sv);
			m_ampShader            = device->createShader(
                nvrhi::ShaderDesc{}
                    .setShaderType(nvrhi::ShaderType::Amplification)
                    .setDebugName("GBuffer Amplification Shader"),
                ampShaderBytecode.data(),
                ampShaderBytecode.size());
		}

		{
			auto bindingLayoutDesc = nvrhi::BindingLayoutDesc{};
			bindingLayoutDesc.setRegisterSpace(BindingSpaces::PerFrameSpace)
				.addItem(m_frameConstants.GetBindingLayoutItem(BindingSlots::CB::FrameConstants))
				.setVisibility(nvrhi::ShaderType::All);

			registry.AttachBindingLayoutItems(bindingLayoutDesc);

			m_bindingLayout = device->createBindingLayout(bindingLayoutDesc);
		}

		auto frameConstantsDesc = DynamicConstantBufferDesc{};
		frameConstantsDesc.AddElement("viewMatrix", ElementType::kFloat4x4)
			.AddElement("projMatrix", ElementType::kFloat4x4)
			.AddElement("instanceCount", ElementType::kUInt)
			.AddElement("meshCount", ElementType::kUInt)
			.SetName("FrameConstantBuffer");

		m_frameConstants = std::move(DynamicConstantBuffer{ device, frameConstantsDesc });
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

			m_bindingSet = device->createBindingSet(bindingSetDesc, m_bindingLayout);
		}
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

					psoDesc.setAmplificationShader(m_ampShader)
						.setMeshShader(m_meshShader)
						.setPixelShader(m_pixelShader)
						.setPrimType(nvrhi::PrimitiveType::TriangleList)
						.setRenderState(renderState)
						.addBindingLayout(m_bindingLayout);

					m_pipeline = device->createMeshletPipeline(psoDesc, frameBufferInfo);
				}

				if (camera.ShouldUpdate())
				{
					m_frameConstants["viewMatrix"] = camera.GetViewMatrix();
					m_frameConstants["projMatrix"] = camera.GetProjMatrix();
				}

				m_frameConstants["instanceCount"] = instanceCount;

				if (registry.Update(m_mainCommandList, device))
				{
					CreateBindingSet(registry, device);
				}

				m_frameConstants.Update(m_mainCommandList);

				state.setPipeline(m_pipeline)
					.addBindingSet(m_bindingSet)
					.setFramebuffer(outputFramebuffer)
					.setViewport(nvrhi::ViewportState{}.addViewportAndScissorRect(
						nvrhi::Viewport{ screenW, screenH }));

				m_mainCommandList->setMeshletState(state);
				m_mainCommandList->dispatchMesh(instanceCount);

				m_mainCommandList->close();
				device->executeCommandList(m_mainCommandList);
			});
	}
}
