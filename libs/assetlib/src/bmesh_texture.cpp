#include "bmesh_texture.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

namespace assetlib
{
	namespace
	{
		uint32_t
		mipCount(uint32_t width, uint32_t height)
		{
			uint32_t levels = 1;
			uint32_t dim    = (std::max)(width, height);
			while (dim > 1)
			{
				dim >>= 1;
				++levels;
			}
			return levels;
		}
	}

	ImageData
	rgba8ToImage(std::span<const std::byte> rgba, uint32_t width, uint32_t height)
	{
		const size_t expected = static_cast<size_t>(width) * height * 4;
		if (rgba.size() < expected)
			throw std::runtime_error("bmesh: RGBA buffer smaller than width*height*4");

		ImageData out;
		out.width     = width;
		out.height    = height;
		out.mipLevels = mipCount(width, height);
		out.arraySize = 1;
		out.vkFormat  = VkFormat::R8G8B8A8_UNORM;
		out.isCubemap = false;

		// Sum every mip so we can pack the chain contiguously (mip-minor, matching D3D12 order).
		size_t totalBytes = 0;
		for (uint32_t mip = 0; mip < out.mipLevels; ++mip)
		{
			const uint32_t mw = (std::max)(1u, width >> mip);
			const uint32_t mh = (std::max)(1u, height >> mip);
			totalBytes += static_cast<size_t>(mw) * mh * 4;
		}

		out.pixels = core::fixed_buffer<std::byte>(totalBytes);
		out.subresources.reserve(out.mipLevels);

		// Level 0 is the source image copied verbatim; each subsequent level is box-downsampled
		// from the previous one in its stored (non-sRGB-aware) space -- matching the old bake.
		std::memcpy(out.pixels.data(), rgba.data(), expected);
		out.subresources.push_back({ 0, static_cast<uint64_t>(width) * 4, expected });

		size_t   prevOffset = 0;
		uint32_t prevW      = width;
		uint32_t prevH      = height;
		size_t   offset     = expected;
		for (uint32_t mip = 1; mip < out.mipLevels; ++mip)
		{
			const uint32_t mw    = (std::max)(1u, width >> mip);
			const uint32_t mh    = (std::max)(1u, height >> mip);
			const size_t   slice = static_cast<size_t>(mw) * mh * 4;

			const auto* src =
				reinterpret_cast<const unsigned char*>(out.pixels.data() + prevOffset);
			auto* dst = reinterpret_cast<unsigned char*>(out.pixels.data() + offset);

			if (stbir_resize_uint8_linear(
					src,
					static_cast<int>(prevW),
					static_cast<int>(prevH),
					0,
					dst,
					static_cast<int>(mw),
					static_cast<int>(mh),
					0,
					STBIR_RGBA) == nullptr)
				throw std::runtime_error("bmesh: mip resize failed");

			out.subresources.push_back({ offset, static_cast<uint64_t>(mw) * 4, slice });

			prevOffset = offset;
			prevW      = mw;
			prevH      = mh;
			offset += slice;
		}

		return out;
	}
}
