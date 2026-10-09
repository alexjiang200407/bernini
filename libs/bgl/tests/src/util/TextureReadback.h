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
#include <cstring>
#include <vector>

namespace bgl::test
{
	/**
	 * A texture's texels as bytes, row-major and tightly packed at `bytesPerTexel`. `resident` is
	 * the layout the last frame left the texture in, which the copy returns it to, so the next
	 * frame's import resumes from it. Drains the renderer first: the copy rides its own queue,
	 * which nothing orders against.
	 */
	inline std::vector<uint8_t>
	ReadTextureBytes(
		IGraphics*                gfx,
		const bgpu::TextureHandle texture,
		const uint32_t            width,
		const uint32_t            height,
		const uint32_t            bytesPerTexel,
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

		const auto*  base     = static_cast<const uint8_t*>(resourceManager->MapReadback(readback));
		const size_t rowBytes = static_cast<size_t>(width) * bytesPerTexel;
		auto         bytes    = std::vector<uint8_t>(rowBytes * height);
		for (uint32_t y = 0; y < height; ++y)
		{
			std::memcpy(
				bytes.data() + y * rowBytes,
				base + layout.offset + y * layout.rowPitch,
				rowBytes);
		}

		resourceManager->UnmapReadback(readback);
		resourceManager->DestroyReadbackBuffer(readback, false);
		return bytes;
	}

	/** An 8-bit four-channel texture's texels, as ReadTextureBytes reads them. */
	inline std::vector<glm::u8vec4>
	ReadRgba8Texels(
		IGraphics*                gfx,
		const bgpu::TextureHandle texture,
		const uint32_t            width,
		const uint32_t            height,
		const bgpu::BarrierLayout resident)
	{
		const std::vector<uint8_t> bytes =
			ReadTextureBytes(gfx, texture, width, height, 4, resident);
		auto texels = std::vector<glm::u8vec4>(static_cast<size_t>(width) * height);
		for (size_t i = 0; i < texels.size(); ++i)
		{
			texels[i] =
				glm::u8vec4(bytes[i * 4], bytes[i * 4 + 1], bytes[i * 4 + 2], bytes[i * 4 + 3]);
		}
		return texels;
	}
}
