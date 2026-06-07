#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "pipeline/GraphicsPipeline.h"
#include "resource/FrameBuffer.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"
#include "types/RenderState.h"

namespace bgl
{
	class TestPass
	{
	public:
		TestPass() = default;
		TestPass(
			IDevice*               device,
			CommandQueueHandle     cmdQueue,
			CommandAllocatorHandle cmdAllocator,
			ResourceManagerHandle  resourceManager)
		{
			Init(device, cmdQueue, cmdAllocator, resourceManager);
		}

		void
		Release(ResourceManagerHandle resourceManager)
		{
			resourceManager->DestroyBuffer(m_VertexBuffer, 0, false);
			m_CommandList.Reset();
			m_Pipeline.Reset();
		}

		void
		Init(
			IDevice*               device,
			CommandQueueHandle     cmdQueue,
			CommandAllocatorHandle cmdAllocator,
			ResourceManagerHandle  resourceManager)
		{
			gassert(device != nullptr, "Device must be initialized");
			gassert(cmdQueue.IsInitialized(), "Command queue must be initialized");
			gassert(cmdAllocator.IsInitialized(), "Command Allocator must be initialized");
			gassert(resourceManager.IsInitialized(), "Resource Manager must be initialized");

			CommandListDesc cmdListDesc;
			cmdListDesc.type = QueueType::kGraphics;
			m_CommandList = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);

			struct Vertex
			{
				float x, y, z;
			};

			const Vertex verts[] = {
				{ 0.0f, 0.5f, 0.0f },    // top
				{ 0.5f, -0.5f, 0.0f },   // bottom right
				{ -0.5f, -0.5f, 0.0f },  // bottom left
			};

			auto bufferDesc      = BufferDesc();
			bufferDesc.byteSize  = sizeof(verts);
			bufferDesc.isUav     = false;
			bufferDesc.debugName = "TestPass Vertex Buffer";

			m_CommandList->Open(cmdQueue, cmdAllocator);
			m_VertexBuffer                = resourceManager->CreateRawBuffer(bufferDesc);
			constexpr uint32_t bufferSize = sizeof(verts);

			m_CommandList->WriteBuffer(m_VertexBuffer, verts, bufferSize);

			m_CommandList->Barrier(
				m_VertexBuffer,
				BufferBarrierDesc()
					.AddSyncBefore(BarrierSyncFlag::kCopy)
					.AddAccessBefore(BarrierAccessFlag::kCopyDest)
					.AddSyncAfter(BarrierSyncFlag::kVertexShader)
					.AddAccessAfter(BarrierAccessFlag::kShaderResource));

			m_CommandList->Close();

			cmdQueue->ExecuteCommandList(m_CommandList);

			auto pipelineDesc              = GraphicsPipelineDesc();
			pipelineDesc.vertexShader      = device->CreateShader("./shaders/Test.vs.dxil"sv);
			pipelineDesc.pixelShader       = device->CreateShader("./shaders/Test.ps.dxil"sv);
			pipelineDesc.rootConstantsSize = 4;
			pipelineDesc.AddRtvFormat(Format::BGRA8_UNORM);
			pipelineDesc.renderState = RenderState{}
			                               .SetRasterState(
											   RasterState{}
												   .SetCullMode(RasterCullMode::kNone)
												   .SetFillMode(RasterFillMode::kSolid))
			                               .SetDepthStencilState(
											   DepthStencilState{}
												   .SetDepthTestEnable(true)
												   .SetDepthWriteEnable(true)
												   .SetDepthFunc(ComparisonFunc::Less)
												   .SetStencilEnable(false));

			m_Pipeline = device->CreateGraphicsPipeline(pipelineDesc);
		}

		uint64_t
		Execute(
			ICommandQueue*     cmdQueue,
			ICommandAllocator* cmdAllocator,
			FrameBuffer        frameBuffer,
			Viewport           vp)
		{
			gassert(cmdQueue != nullptr, "Pass command queue must be initialized");
			gassert(cmdAllocator != nullptr, "Pass command allocator must be initialized");
			gassert(m_CommandList.IsInitialized(), "Pass commandlist must be initialized");
			gassert(m_Pipeline.IsInitialized(), "Pass pipeline must be initialized");

			m_CommandList->Open(cmdQueue, cmdAllocator);

			struct RootConstantsData
			{
				uint32_t vertexBufferHeapIndex;
			} rootConstantsData(m_VertexBuffer.idx);

			auto gfxState     = GraphicsState();
			gfxState.pipeline = m_Pipeline;
			gfxState.viewportState.AddViewportAndScissorRect(vp);
			gfxState.frameBuffer      = frameBuffer;
			gfxState.rootConstantData = &rootConstantsData;
			gfxState.rootConstantSize = sizeof(rootConstantsData);

			m_CommandList->SetGraphicsState(gfxState);

			{
				auto barrierDesc = TextureBarrierDesc();
				barrierDesc.AddAccessBefore(BarrierAccessFlag::kNone)
					.AddSyncAfter(BarrierSyncFlag::kNone)
					.SetLayoutBefore(BarrierLayout::kPresent)
					.AddAccessAfter(BarrierAccessFlag::kRenderTarget)
					.AddSyncAfter(BarrierSyncFlag::kRenderTarget)
					.SetLayoutAfter(BarrierLayout::kRenderTarget);

				m_CommandList->Barrier(frameBuffer.colorAttachments[0], barrierDesc);
			}

			m_CommandList->DrawInstanced(3, 1);

			{
				auto barrierDesc = TextureBarrierDesc();
				barrierDesc.AddAccessBefore(BarrierAccessFlag::kRenderTarget)
					.AddSyncBefore(BarrierSyncFlag::kRenderTarget)
					.SetLayoutBefore(BarrierLayout::kRenderTarget)
					.AddAccessAfter(BarrierAccessFlag::kNone)
					.AddSyncAfter(BarrierSyncFlag::kNone)
					.SetLayoutAfter(BarrierLayout::kPresent);

				m_CommandList->Barrier(frameBuffer.colorAttachments[0], barrierDesc);
			}

			m_CommandList->Close();

			return cmdQueue->ExecuteCommandList(m_CommandList);
		}

	private:
		BufferHandle           m_VertexBuffer;
		CommandListHandle      m_CommandList;
		GraphicsPipelineHandle m_Pipeline;
	};
}
