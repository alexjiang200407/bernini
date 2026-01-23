#pragma once
#include "BindingSlots.h"
#include "buffer/StructuredBufferGPU.h"
#include "buffer/StructuredUploadBuffer.h"
#include "camera/Camera.h"
#include "frame_graph/FrameGraphView.h"
#include "mesh/DrawIndexedArgs.h"
#include "mesh/Mesh.h"
#include "mesh/Vertex.h"
//#include "passes/FrameData.h"
#include <core/file/file.h>
#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

class FrameGraph;
class FrameGraphBlackboard;

namespace gfx
{
	struct FrameData;
	class MeshRegistry;

	struct RenderArgs
	{
		float                    screenWidth;
		float                    screenHeight;
		nvrhi::DeviceHandle      device;
		nvrhi::FramebufferHandle outBuffer;
		nvrhi::FramebufferInfo   outBufferInfo;
	};

	static inline auto renderState = nvrhi::RenderState{}
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

	class GBufferPass
	{
	public:
		void
		Init(nvrhi::DeviceHandle device)
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
		}

		void
		AttachToFrameGraph(
			FrameGraph&           frameGraph,
			FrameGraphBlackboard& blackBoard,
			RenderArgs            renderArgs)
		{
			auto device            = renderArgs.device;
			auto screenW           = renderArgs.screenWidth;
			auto screenH           = renderArgs.screenHeight;
			auto outputFramebuffer = renderArgs.outBuffer;
			auto frameBufferInfo   = renderArgs.outBufferInfo;

			m_mainCommandList->open();

			auto state = nvrhi::MeshletState{};

			// TODO: Could move this into init?
			if (!m_pipeline)
			{
				auto psoDesc        = nvrhi::MeshletPipelineDesc{};
				psoDesc.AS          = m_ampShader;
				psoDesc.MS          = m_meshShader;
				psoDesc.PS          = m_pixelShader;
				psoDesc.primType    = nvrhi::PrimitiveType::TriangleList;
				psoDesc.renderState = renderState;

				m_pipeline = device->createMeshletPipeline(psoDesc, frameBufferInfo);
			}

			state.pipeline = m_pipeline;
			state.setFramebuffer(outputFramebuffer)
				.setViewport(
					nvrhi::ViewportState{}.addViewportAndScissorRect(
						nvrhi::Viewport{ screenW, screenH }));

			m_mainCommandList->setMeshletState(state);
			m_mainCommandList->dispatchMesh(1);

			m_mainCommandList->close();
			device->executeCommandList(m_mainCommandList);
		}

	private:
		nvrhi::CommandListHandle     m_mainCommandList;
		nvrhi::MeshletPipelineHandle m_pipeline;
		nvrhi::ShaderHandle          m_meshShader;
		nvrhi::ShaderHandle          m_ampShader;
		nvrhi::ShaderHandle          m_pixelShader;
	};
}
