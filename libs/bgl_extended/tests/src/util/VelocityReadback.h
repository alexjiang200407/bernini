#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "gfx/RenderTargetBase.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "resource/Texture.h"
#include "types/Barrier.h"
#include "types/QueueType.h"
#include "util/HalfFloat.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bgl::test
{
	/**
	 * The target's velocity buffer as one float2 per pixel, row-major and tightly packed.
	 *
	 * Drains the renderer first -- the copy rides its own queue, which nothing orders against --
	 * and returns the texture to render-target layout afterwards, where the forward pass leaves it
	 * between frames.
	 */
	inline std::vector<glm::vec2>
	ReadMotionVectors(IGraphics* gfx, IRenderTarget* target, uint32_t width, uint32_t height)
	{
		auto* gfxBase    = gfx->As<GraphicsBase>();
		auto* targetBase = target->As<RenderTargetBase>();

		auto resourceManager = gfxBase->GetResourceManagerCpy();

		const TextureHandle texture = targetBase->GetMotionVectorTexture();
		const auto          layout  = resourceManager->GetTextureReadbackLayout(texture);

		auto rbDesc      = ReadbackBufferDesc();
		rbDesc.byteSize  = layout.totalBytes;
		rbDesc.debugName = "Motion Vector Readback";

		auto readback = resourceManager->CreateReadbackBuffer(rbDesc);

		gfxBase->WaitIdle();

		auto cmdListDesc = CommandListDesc();
		cmdListDesc.type = QueueType::kGraphics;

		auto* device       = gfxBase->GetDevice();
		auto  cmdAllocator = device->CreateCommandAllocator();
		auto  cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto  cmdQueue     = device->CreateCommandQueue(QueueType::kGraphics);

		cmdAllocator->ResetAllocator();
		cmdList->Open(cmdQueue, cmdAllocator);

		const auto transition = [&](BarrierLayout before, BarrierLayout after) {
			auto barrier = TextureBarrierDesc();
			barrier.AddSyncBefore(BarrierSyncFlag::kAllCommands)
				.AddAccessBefore(
					before == BarrierLayout::kRenderTarget ? BarrierAccessFlag::kRenderTarget :
															 BarrierAccessFlag::kCopySource)
				.SetLayoutBefore(before)
				.AddSyncAfter(BarrierSyncFlag::kAllCommands)
				.AddAccessAfter(
					after == BarrierLayout::kRenderTarget ? BarrierAccessFlag::kRenderTarget :
															BarrierAccessFlag::kCopySource)
				.SetLayoutAfter(after);
			cmdList->Barrier(texture, barrier);
		};

		transition(BarrierLayout::kRenderTarget, BarrierLayout::kCopySource);
		cmdList->CopyTextureToReadback(readback, texture);
		transition(BarrierLayout::kCopySource, BarrierLayout::kRenderTarget);

		cmdList->Close();
		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

		const auto* base = static_cast<const uint8_t*>(resourceManager->MapReadback(readback));

		auto motion = std::vector<glm::vec2>(static_cast<size_t>(width) * height);
		for (uint32_t y = 0; y < height; ++y)
		{
			const auto* row =
				reinterpret_cast<const uint16_t*>(base + layout.offset + y * layout.rowPitch);

			for (uint32_t x = 0; x < width; ++x)
			{
				motion[static_cast<size_t>(y) * width + x] =
					glm::vec2(HalfToFloat(row[x * 2]), HalfToFloat(row[x * 2 + 1]));
			}
		}

		resourceManager->UnmapReadback(readback);
		resourceManager->DestroyReadbackBuffer(readback, false);

		return motion;
	}
}
