#pragma once
#include "gfx/GraphicsBase.h"
#include <bgl/IGraphics.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/QueueType.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bgl::test
{
	/**
	 * An 8-bit four-channel texture's texels, row-major and tightly packed, as bytes. `resident` is
	 * the layout the last frame left the texture in, which the copy returns it to, so the next
	 * frame's import resumes from it. Drains the renderer first: the copy rides its own queue.
	 */
	inline std::vector<glm::u8vec4>
	ReadRgba8Texels(
		IGraphics*                gfx,
		const bgpu::TextureHandle texture,
		const uint32_t            width,
		const uint32_t            height,
		const bgpu::BarrierLayout resident)
	{
		auto*      gfxBase         = gfx->As<GraphicsBase>();
		auto       resourceManager = gfxBase->GetResourceManagerCpy();
		const auto layout          = resourceManager->GetTextureReadbackLayout(texture);

		auto rbDesc      = bgpu::ReadbackBufferDesc();
		rbDesc.byteSize  = layout.totalBytes;
		rbDesc.debugName = "Texture Readback";
		auto readback    = resourceManager->CreateReadbackBuffer(rbDesc);

		gfxBase->WaitIdle();

		auto cmdListDesc   = bgpu::CommandListDesc();
		cmdListDesc.type   = bgpu::QueueType::kGraphics;
		auto* device       = gfxBase->GetDevice();
		auto  cmdAllocator = device->CreateCommandAllocator();
		auto  cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto  cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

		cmdAllocator->ResetAllocator();
		cmdList->Open(cmdQueue, cmdAllocator);

		const auto accessFor = [](const bgpu::BarrierLayout of) {
			if (of == bgpu::BarrierLayout::kRenderTarget)
			{
				return bgpu::BarrierAccessFlag::kRenderTarget;
			}
			if (of == bgpu::BarrierLayout::kShaderResource)
			{
				return bgpu::BarrierAccessFlag::kShaderResource;
			}
			return bgpu::BarrierAccessFlag::kCopySource;
		};
		const auto transition = [&](const bgpu::BarrierLayout before,
		                            const bgpu::BarrierLayout after) {
			auto barrier = bgpu::TextureBarrierDesc();
			barrier.AddSyncBefore(bgpu::BarrierSyncFlag::kAllCommands)
				.AddAccessBefore(accessFor(before))
				.SetLayoutBefore(before)
				.AddSyncAfter(bgpu::BarrierSyncFlag::kAllCommands)
				.AddAccessAfter(accessFor(after))
				.SetLayoutAfter(after);
			cmdList->Barrier(texture, barrier);
		};

		transition(resident, bgpu::BarrierLayout::kCopySource);
		cmdList->CopyTextureToReadback(readback, texture);
		transition(bgpu::BarrierLayout::kCopySource, resident);

		cmdList->Close();
		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

		const auto* base   = static_cast<const uint8_t*>(resourceManager->MapReadback(readback));
		auto        texels = std::vector<glm::u8vec4>(static_cast<size_t>(width) * height);
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
