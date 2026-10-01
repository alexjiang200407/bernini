#pragma once
#include "gfx/GraphicsBase.h"
#include "gfx/RenderTargetBase.h"
#include "util/HalfFloat.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/QueueType.h>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <vector>

namespace bgl::test
{
	/**
	 * The target's velocity buffer as one float4 per pixel, row-major and tightly packed: the
	 * velocity in xy and the surface's own motion in zw.
	 *
	 * Drains the renderer first -- the copy rides its own queue, which nothing orders against --
	 * and returns the texture to the layout the frame left it in, which is what the next frame's
	 * import resumes from.
	 */
	inline std::vector<glm::vec4>
	ReadVelocityTexels(IGraphics* gfx, IRenderTarget* target, uint32_t width, uint32_t height)
	{
		auto* gfxBase    = gfx->As<GraphicsBase>();
		auto* targetBase = target->As<RenderTargetBase>();

		auto resourceManager = gfxBase->GetResourceManagerCpy();

		const bgpu::TextureHandle texture = targetBase->GetMotionVectorTexture();
		const auto                layout  = resourceManager->GetTextureReadbackLayout(texture);

		auto rbDesc      = bgpu::ReadbackBufferDesc();
		rbDesc.byteSize  = layout.totalBytes;
		rbDesc.debugName = "Motion Vector Readback";

		auto readback = resourceManager->CreateReadbackBuffer(rbDesc);

		gfxBase->WaitIdle();

		auto cmdListDesc = bgpu::CommandListDesc();
		cmdListDesc.type = bgpu::QueueType::kGraphics;

		auto* device       = gfxBase->GetDevice();
		auto  cmdAllocator = device->CreateCommandAllocator();
		auto  cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto  cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

		cmdAllocator->ResetAllocator();
		cmdList->Open(cmdQueue, cmdAllocator);

		const auto accessFor = [](bgpu::BarrierLayout layout) {
			if (layout == bgpu::BarrierLayout::kRenderTarget)
			{
				return bgpu::BarrierAccessFlag::kRenderTarget;
			}
			if (layout == bgpu::BarrierLayout::kShaderResource)
			{
				return bgpu::BarrierAccessFlag::kShaderResource;
			}
			return bgpu::BarrierAccessFlag::kCopySource;
		};

		const auto transition = [&](bgpu::BarrierLayout before, bgpu::BarrierLayout after) {
			auto barrier = bgpu::TextureBarrierDesc();
			barrier.AddSyncBefore(bgpu::BarrierSyncFlag::kAllCommands)
				.AddAccessBefore(accessFor(before))
				.SetLayoutBefore(before)
				.AddSyncAfter(bgpu::BarrierSyncFlag::kAllCommands)
				.AddAccessAfter(accessFor(after))
				.SetLayoutAfter(after);
			cmdList->Barrier(texture, barrier);
		};

		// The resolve samples the velocity buffer, so a target with TAA leaves it in
		// shader-resource; without one the forward pass's render-target layout is the last set.
		const bgpu::BarrierLayout resident = target->IsTaaEnabled() ?
		                                         bgpu::BarrierLayout::kShaderResource :
		                                         bgpu::BarrierLayout::kRenderTarget;

		transition(resident, bgpu::BarrierLayout::kCopySource);
		cmdList->CopyTextureToReadback(readback, texture);
		transition(bgpu::BarrierLayout::kCopySource, resident);

		cmdList->Close();
		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

		const auto* base = static_cast<const uint8_t*>(resourceManager->MapReadback(readback));

		auto texels = std::vector<glm::vec4>(static_cast<size_t>(width) * height);
		for (uint32_t y = 0; y < height; ++y)
		{
			const auto* row =
				reinterpret_cast<const uint16_t*>(base + layout.offset + y * layout.rowPitch);

			for (uint32_t x = 0; x < width; ++x)
			{
				texels[static_cast<size_t>(y) * width + x] = glm::vec4(
					HalfToFloat(row[x * 4]),
					HalfToFloat(row[x * 4 + 1]),
					HalfToFloat(row[x * 4 + 2]),
					HalfToFloat(row[x * 4 + 3]));
			}
		}

		resourceManager->UnmapReadback(readback);
		resourceManager->DestroyReadbackBuffer(readback, false);

		return texels;
	}

	/** The velocity half of ReadVelocityTexels: where each pixel's surface sat last frame. */
	inline std::vector<glm::vec2>
	ReadMotionVectors(IGraphics* gfx, IRenderTarget* target, uint32_t width, uint32_t height)
	{
		return ReadVelocityTexels(gfx, target, width, height) |
		       std::views::transform(
				   [](const glm::vec4& texel) { return glm::vec2(texel.x, texel.y); }) |
		       std::ranges::to<std::vector>();
	}

	/** The other half: the part of each pixel's velocity its surface moved by on its own. */
	inline std::vector<glm::vec2>
	ReadOwnMotion(IGraphics* gfx, IRenderTarget* target, uint32_t width, uint32_t height)
	{
		return ReadVelocityTexels(gfx, target, width, height) |
		       std::views::transform(
				   [](const glm::vec4& texel) { return glm::vec2(texel.z, texel.w); }) |
		       std::ranges::to<std::vector>();
	}
}
