#include "passes/GBufferPass.h"
#include "camera/Camera.h"
#include "fg/FrameGraph.hpp"

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
	GBufferPass::AttachToFrameGraph(
		FrameGraph&                           frameGraph,
		FrameGraphBlackboard&                 blackBoard,
		RenderArgs                            renderArgs,
		Camera&                               camera,
		std::span<std::unique_ptr<IDrawable>> drawables)
	{
		(void)blackBoard;

		frameGraph.addCallbackPass(
			"GBufferPass",
			[](FrameGraph::Builder& builder, auto&) { builder.setSideEffect(); },
			[this, renderArgs, drawables, &camera](auto&, FrameGraphPassResources&, void*) {
				auto device            = renderArgs.device;
				auto frameBufferInfo   = renderArgs.outBufferInfo;
				auto screenW           = renderArgs.screenWidth;
				auto screenH           = renderArgs.screenHeight;
				auto outputFramebuffer = renderArgs.outBuffer;

				auto mainCommandList = device->createCommandList();

				GeomPass::UpdateCameraBuffer(mainCommandList, camera);

				auto pipelineDesc = nvrhi::GraphicsPipelineDesc{};

				pipelineDesc.setRenderState(renderState);

				nvrhi::GraphicsState gfxState =
					nvrhi::GraphicsState{}
						.setFramebuffer(outputFramebuffer)
						.setViewport(
							nvrhi::ViewportState{}.addViewportAndScissorRect(
								nvrhi::Viewport{ screenW, screenH }));

				auto bindingSetDesc          = nvrhi::BindingSetDesc{};
				auto vertexBindingLayoutDesc = nvrhi::BindingLayoutDesc{};
				GeomPass::AttachBindingSetItems(bindingSetDesc);
				GeomPass::AttachVertexBindingLayoutItems(vertexBindingLayoutDesc);

				for (const auto& drawable : drawables)
				{
					drawable->AttachBindingLayoutItems(bindingSetDesc);
					GeomPass::UpdateTransformBuffer(mainCommandList, drawable->GetTransform());

					auto bindingLayout = device->createBindingLayout(vertexBindingLayoutDesc);
					auto bindingSet    = device->createBindingSet(bindingSetDesc, bindingLayout);

					pipelineDesc.addBindingLayout(bindingLayout);
					gfxState.addBindingSet(bindingSet);

					drawable->AttachVertexLayout(pipelineDesc);

					drawable->AttachVertexShader(pipelineDesc);
					drawable->AttachPixelShader(pipelineDesc);

					nvrhi::GraphicsPipelineHandle graphicsPipeline =
						device->createGraphicsPipeline(pipelineDesc, frameBufferInfo);

					gfxState.setPipeline(graphicsPipeline);

					auto params = DrawParams{
						.gfxState    = gfxState,
						.device      = device,
						.commandList = mainCommandList,
					};

					drawable->Draw(params);
				}

				mainCommandList->close();
				device->executeCommandList(mainCommandList);
			});
	}

}
