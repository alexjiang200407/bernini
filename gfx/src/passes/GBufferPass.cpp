#include "passes/GBufferPass.h"
#include "BindingSlots.h"
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
		FrameGraph&                          frameGraph,
		FrameGraphBlackboard&                blackBoard,
		RenderArgs                           renderArgs,
		Camera&                              camera,
		std::span<std::unique_ptr<Drawable>> drawables)
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
				mainCommandList->open();

				GeomPass::UpdateCameraBuffer(mainCommandList, camera);

				auto pipelineDesc = nvrhi::GraphicsPipelineDesc{};

				pipelineDesc.setRenderState(renderState);

				nvrhi::GraphicsState gfxState =
					nvrhi::GraphicsState{}
						.setFramebuffer(outputFramebuffer)
						.setViewport(
							nvrhi::ViewportState{}.addViewportAndScissorRect(
								nvrhi::Viewport{ screenW, screenH }));

				auto perFrameBindingLayout = nvrhi::BindingLayoutHandle{};
				auto perFrameBindingSet    = nvrhi::BindingSetHandle{};

				{
					auto perFrameBindingSetDesc    = nvrhi::BindingSetDesc{};
					auto perFrameBindingLayoutDesc = nvrhi::BindingLayoutDesc{};

					perFrameBindingLayoutDesc.setRegisterSpace(BindingSpaces::PerFrameSpace);
					AttachPerFrameBindingSetItems(perFrameBindingSetDesc);
					AttachPerFrameBindingLayoutItems(perFrameBindingLayoutDesc);

					perFrameBindingLayout = device->createBindingLayout(perFrameBindingLayoutDesc);

					perFrameBindingSet =
						device->createBindingSet(perFrameBindingSetDesc, perFrameBindingLayout);

					pipelineDesc.addBindingLayout(perFrameBindingLayout);
					gfxState.addBindingSet(perFrameBindingSet);
				}

				for (const auto& drawable : drawables)
				{
					auto perObjBindingLayout = nvrhi::BindingLayoutHandle{};
					auto perObjBindingSet    = nvrhi::BindingSetHandle{};

					{
						auto perObjBindingSetDesc    = nvrhi::BindingSetDesc{};
						auto perObjBindingLayoutDesc = nvrhi::BindingLayoutDesc{};

						perObjBindingLayoutDesc.setRegisterSpace(BindingSpaces::PerObjectSpace);
						AttachPerObjBindingSetItems(perObjBindingSetDesc);
						AttachPerObjBindingLayoutItems(perObjBindingLayoutDesc);

						perObjBindingLayout = device->createBindingLayout(perObjBindingLayoutDesc);

						perObjBindingSet =
							device->createBindingSet(perObjBindingSetDesc, perObjBindingLayout);

						pipelineDesc.addBindingLayout(perObjBindingLayout);
						gfxState.addBindingSet(perObjBindingSet);
					}

					GeomPass::UpdateTransformBuffer(mainCommandList, drawable->GetTransform());

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
