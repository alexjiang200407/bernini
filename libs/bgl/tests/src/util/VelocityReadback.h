#pragma once
#include "gfx/RenderTargetBase.h"
#include "util/HalfFloat.h"
#include "util/TextureReadback.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
		auto* targetBase = target->As<RenderTargetBase>();

		// The resolve samples the velocity buffer, so a target with TAA leaves it in
		// shader-resource; without one the forward pass's render-target layout is the last set.
		const bgpu::BarrierLayout  resident = target->IsTaaEnabled() ?
		                                          bgpu::BarrierLayout::kShaderResource :
		                                          bgpu::BarrierLayout::kRenderTarget;
		const std::vector<uint8_t> bytes =
			ReadTextureBytes(gfx, targetBase->GetMotionVectorTexture(), width, height, 8, resident);

		auto texels = std::vector<glm::vec4>(static_cast<size_t>(width) * height);
		for (size_t i = 0; i < texels.size(); ++i)
		{
			uint16_t half[4];
			std::memcpy(half, bytes.data() + i * 8, sizeof(half));
			texels[i] = glm::vec4(
				HalfToFloat(half[0]),
				HalfToFloat(half[1]),
				HalfToFloat(half[2]),
				HalfToFloat(half[3]));
		}
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
