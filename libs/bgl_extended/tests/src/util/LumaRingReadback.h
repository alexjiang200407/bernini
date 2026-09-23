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
#include "types/TaaHistoryPlane.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bgl::test
{
	/**
	 * The luma ring the last frame's resolve wrote, one byte per entry, row-major and tightly
	 * packed: x the newest frame, w the oldest.
	 *
	 * Drains the renderer first and returns the texture to render-target layout, which is where the
	 * resolve left it and what the next frame's import resumes from.
	 *
	 * @pre A TAA frame has been drawn on `target`.
	 */
	inline std::vector<glm::u8vec4>
	ReadLumaRing(IGraphics* gfx, IRenderTarget* target, uint32_t width, uint32_t height)
	{
		auto* gfxBase    = gfx->As<GraphicsBase>();
		auto* targetBase = target->As<RenderTargetBase>();

		auto resourceManager = gfxBase->GetResourceManagerCpy();

		// The frame's end swapped the slots, so the one just written is the one the next frame reads.
		const uint32_t      written = targetBase->GetCurrentHistoryIndex() ^ 1u;
		const TextureHandle texture =
			targetBase->GetHistoryTexture(written, TaaHistoryPlane::kLumaRing);
		const auto layout = resourceManager->GetTextureReadbackLayout(texture);

		auto rbDesc      = ReadbackBufferDesc();
		rbDesc.byteSize  = layout.totalBytes;
		rbDesc.debugName = "Luma Ring Readback";

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

		const auto transition = [&](BarrierLayout     before,
		                            BarrierAccessFlag beforeAccess,
		                            BarrierLayout     after,
		                            BarrierAccessFlag afterAccess) {
			auto barrier = TextureBarrierDesc();
			barrier.AddSyncBefore(BarrierSyncFlag::kAllCommands)
				.AddAccessBefore(beforeAccess)
				.SetLayoutBefore(before)
				.AddSyncAfter(BarrierSyncFlag::kAllCommands)
				.AddAccessAfter(afterAccess)
				.SetLayoutAfter(after);
			cmdList->Barrier(texture, barrier);
		};

		transition(
			BarrierLayout::kRenderTarget,
			BarrierAccessFlag::kRenderTarget,
			BarrierLayout::kCopySource,
			BarrierAccessFlag::kCopySource);
		cmdList->CopyTextureToReadback(readback, texture);
		transition(
			BarrierLayout::kCopySource,
			BarrierAccessFlag::kCopySource,
			BarrierLayout::kRenderTarget,
			BarrierAccessFlag::kRenderTarget);

		cmdList->Close();
		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

		const auto* base = static_cast<const uint8_t*>(resourceManager->MapReadback(readback));

		auto texels = std::vector<glm::u8vec4>(static_cast<size_t>(width) * height);
		for (uint32_t y = 0; y < height; ++y)
		{
			const uint8_t* row = base + layout.offset + y * layout.rowPitch;

			for (uint32_t x = 0; x < width; ++x)
			{
				texels[static_cast<size_t>(y) * width + x] =
					glm::u8vec4(row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]);
			}
		}

		resourceManager->UnmapReadback(readback);
		resourceManager->DestroyReadbackBuffer(readback, false);

		return texels;
	}
}
