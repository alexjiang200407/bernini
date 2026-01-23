//#include "passes/old/GBufferPass.h"
//#include "BindingSlots.h"
//#include "buffer/StructuredBufferGPU.h"
//#include "buffer/StructuredUploadBuffer.h"
//#include "camera/Camera.h"
//#include "frame_graph/FrameGraphView.h"
//#include "mesh/DrawIndexedArgs.h"
//#include "mesh/Mesh.h"
//#include "mesh/Vertex.h"
//#include "passes/FrameData.h"
//#include <core/file/file.h>
//#include <fg/Blackboard.hpp>
//#include <fg/FrameGraph.hpp>
//
//namespace gfx
//{
//	static auto renderState = nvrhi::RenderState{}
//	                              .setRasterState(
//									  nvrhi::RasterState{}
//										  .setCullMode(nvrhi::RasterCullMode::None)
//										  .setFillMode(nvrhi::RasterFillMode::Solid))
//	                              .setDepthStencilState(
//									  nvrhi::DepthStencilState{}
//										  .setDepthTestEnable(true)
//										  .setDepthWriteEnable(true)
//										  .setDepthFunc(nvrhi::ComparisonFunc::Less)
//										  .setStencilEnable(false));
//
//	void
//	GBufferPass::Init(nvrhi::DeviceHandle device)
//	{
//		m_mainCommandList = device->createCommandList();
//
//		{
//			auto vertexShaderBytecode = core::file::readFileBytes("shaders/VS_GBuffer.cso"sv);
//			m_vertexShader            = device->createShader(
//                nvrhi::ShaderDesc{}
//                    .setShaderType(nvrhi::ShaderType::Vertex)
//                    .setDebugName("GBuffer Vertex Shader"),
//                vertexShaderBytecode.data(),
//                vertexShaderBytecode.size());
//		}
//
//		{
//			auto pixelShaderBytecode = core::file::readFileBytes("shaders/PS_GBuffer.cso"sv);
//			m_pixelShader            = device->createShader(
//                nvrhi::ShaderDesc{}
//                    .setShaderType(nvrhi::ShaderType::Pixel)
//                    .setDebugName("GBuffer Pixel Shader"),
//                pixelShaderBytecode.data(),
//                pixelShaderBytecode.size());
//		}
//	}
//
//	void
//	GBufferPass::AttachToFrameGraph(
//		FrameGraph&           frameGraph,
//		FrameGraphBlackboard& blackBoard,
//		RenderArgs            renderArgs)
//	{
//		const auto frameData = blackBoard.get<FrameData>();
//
//		frameGraph.addCallbackPass(
//			"GBufferPass",
//			[=](FrameGraph::Builder& builder, auto&) {
//				builder.read(frameData.frameConstantsBindingSet);
//				builder.read(frameData.frameConstantsBindingLayout);
//				builder.read(frameData.drawIndirectArgs);
//				builder.read(frameData.drawIndirectCount);
//				builder.read(frameData.compactedInstances);
//				builder.read(frameData.indexBuffer);
//				builder.read(frameData.vertexBuffer);
//				builder.setSideEffect();
//			},
//			[this, renderArgs, frameData](auto&, FrameGraphPassResources& resources, void*) {
//				auto device            = renderArgs.device;
//				auto screenW           = renderArgs.screenWidth;
//				auto screenH           = renderArgs.screenHeight;
//				auto outputFramebuffer = renderArgs.outBuffer;
//
//				if (!m_graphicsPipeline)
//				{
//					auto& layout = resources.get<FrameGraphView<nvrhi::BindingLayoutHandle>>(
//						frameData.frameConstantsBindingLayout);
//
//					nvrhi::GraphicsPipelineDesc desc{};
//					desc.setVertexShader(m_vertexShader)
//						.setPixelShader(m_pixelShader)
//						.setRenderState(renderState)
//						.addBindingLayout(layout.Get())
//						.setInputLayout(device->createInputLayout(nullptr, 0, m_vertexShader));
//
//					m_graphicsPipeline = device->createGraphicsPipeline(desc, outputFramebuffer);
//				}
//
//				auto  gfxState       = nvrhi::GraphicsState{};
//				auto& drawBindingSet = resources.get<FrameGraphView<nvrhi::BindingSetHandle>>(
//					frameData.frameConstantsBindingSet);
//				gfxState.addBindingSet(drawBindingSet.Get());
//
//				gfxState.setFramebuffer(outputFramebuffer)
//					.setViewport(
//						nvrhi::ViewportState{}.addViewportAndScissorRect(
//							nvrhi::Viewport{ screenW, screenH }))
//					.setPipeline(m_graphicsPipeline);
//
//				m_mainCommandList->open();
//
//				auto& drawArgsBuffer =
//					resources
//						.get<StructuredBufferGPU<DrawIndexedArgs>::View>(frameData.drawIndirectArgs)
//						.Get();
//				auto& drawCountBuffer =
//					resources.get<StructuredBufferGPU<uint32_t>::View>(frameData.drawIndirectCount)
//						.Get();
//
//				auto& compactedInstances = resources
//			                                   .get<StructuredBufferGPU<Mesh::Instance>::View>(
//												   frameData.compactedInstances)
//			                                   .Get();
//
//				auto& vertexBuffer =
//					resources.get<StructuredUploadBufferView<Vertex>>(frameData.vertexBuffer).Get();
//				auto& indexBuffer =
//					resources.get<StructuredUploadBufferView<uint32_t>>(frameData.indexBuffer)
//						.Get();
//
//				m_mainCommandList->setResourceStatesForFramebuffer(outputFramebuffer.Get());
//
//				gfxState.setIndexBuffer({ indexBuffer.GetBuffer(), nvrhi::Format::R32_UINT, 0 })
//					.addVertexBuffer({ vertexBuffer.GetBuffer(), 0, 0 })
//					.setIndirectParams(drawArgsBuffer.GetBuffer())
//					.setIndirectCount(drawCountBuffer.GetBuffer())
//					.setMaxDrawCount(1024);
//
//				m_mainCommandList->setGraphicsState(gfxState);
//
//				m_mainCommandList->drawIndexedIndirect();
//
//				m_mainCommandList->close();
//				device->executeCommandList(m_mainCommandList);
//			});
//	}
//}
