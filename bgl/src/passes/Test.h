#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "pipeline/MeshletPipeline.h"
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

		TestPass(const TestPass&) noexcept = delete;
		TestPass(TestPass&&) noexcept      = delete;

		TestPass&
		operator=(const TestPass&) noexcept = delete;

		TestPass&
		operator=(TestPass&&) noexcept = delete;

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

			auto bufferDesc         = BufferDesc();
			bufferDesc.elementCount = std::size(verts);
			bufferDesc.stride       = sizeof(Vertex);
			bufferDesc.isUav        = false;
			bufferDesc.debugName    = "TestPass Vertex Buffer";

			m_CommandList->Open(cmdQueue, cmdAllocator);
			m_VertexBuffer                = resourceManager->CreateStructBuffer(bufferDesc);
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

			auto pipelineDesc        = MeshletPipelineDesc();
			pipelineDesc.meshShader  = device->CreateShader("./shaders/MSTest.dxil", "MSTest");
			pipelineDesc.pixelShader = device->CreateShader("./shaders/PSTest.dxil", "PSTest");

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

			m_Pipeline = device->CreateMeshletPipeline(pipelineDesc);
			m_Uniforms = device->CreateUniforms(m_Pipeline.Get());
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

			m_Uniforms["vertexBuffer"] = DescriptorHandle(m_VertexBuffer.idx);
			m_Uniforms["color"]        = glm::vec3(1.0, 0.0, 0.0);

			auto gfxState     = MeshletState();
			gfxState.pipeline = m_Pipeline;
			gfxState.viewportState.AddViewportAndScissorRect(vp);
			gfxState.frameBuffer = frameBuffer;
			gfxState.uniforms    = &m_Uniforms;

			m_CommandList->SetMeshletState(gfxState);

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

			m_CommandList->DispatchMesh(3, 1, 1);

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
		BufferHandle          m_VertexBuffer;
		CommandListHandle     m_CommandList;
		MeshletPipelineHandle m_Pipeline;
		Uniforms              m_Uniforms;
	};
}
