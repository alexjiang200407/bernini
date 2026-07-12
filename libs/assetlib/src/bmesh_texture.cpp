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

		/**
		 * One alpha byte, scaled and re-quantized -- exactly the value that will be *stored*.
		 *
		 */
		uint8_t
		scaledAlpha(std::byte alpha, float scale)
		{
			const float scaled = static_cast<float>(std::to_integer<uint8_t>(alpha)) * scale;
			return static_cast<uint8_t>(std::lround((std::min)(scaled, 255.0f)));
		}

		// The fraction of texels this level would keep, if every alpha were first scaled by `scale`.
		// This is exactly what the shader's alpha test asks of the level, so it is the quantity that
		// has to hold constant down the chain.
		double
		alphaCoverage(std::span<const std::byte> rgba, float cutoff, float scale)
		{
			const size_t texels = rgba.size() / 4;
			if (texels == 0)
				return 0.0;

			size_t passed = 0;
			for (size_t t = 0; t < texels; ++t)
			{
				const float alpha =
					static_cast<float>(scaledAlpha(rgba[t * 4 + 3], scale)) / 255.0f;
				if (alpha >= cutoff)
					++passed;
			}
			return static_cast<double>(passed) / static_cast<double>(texels);
		}

		/**
		 * Rescales `rgba`'s alpha so the fraction of texels passing `cutoff` matches `targetCoverage`
		 * (mip 0's).
		 *
		 */
		void
		matchAlphaCoverage(std::span<std::byte> rgba, float cutoff, double targetCoverage)
		{
			constexpr int   c_Iterations = 12;
			constexpr float c_MaxScale   = 8.0f;

			float lo = 0.0f;
			float hi = c_MaxScale;

			// A mask that has averaged away to nothing cannot be scaled back: 0 * anything is 0. Give
			// up rather than loop, and leave the level as it is.
			if (alphaCoverage(rgba, cutoff, c_MaxScale) < targetCoverage)
				return;

			for (int i = 0; i < c_Iterations; ++i)
			{
				const float mid = 0.5f * (lo + hi);
				if (alphaCoverage(rgba, cutoff, mid) < targetCoverage)
					lo = mid;  // too few texels survive: let more through
				else
					hi = mid;
			}

			const size_t texels = rgba.size() / 4;
			for (size_t t = 0; t < texels; ++t)
			{
				std::byte& alpha = rgba[t * 4 + 3];
				alpha            = static_cast<std::byte>(scaledAlpha(alpha, hi));
			}
		}
	}

	ImageData
	rgba8ToImage(
		std::span<const std::byte> rgba,
		uint32_t                   width,
		uint32_t                   height,
		std::optional<float>       alphaCutoff)
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

		// The coverage every smaller level has to reproduce. Measured on mip 0 at scale 1, i.e. on the
		// mask exactly as it was authored.
		const double targetCoverage =
			alphaCutoff ? alphaCoverage(rgba.first(expected), *alphaCutoff, 1.0f) : 0.0;

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

			// Downsample first, then restore the coverage the averaging just ate. Each level is
			// corrected against *mip 0*, not against its parent, so the error cannot compound down the
			// chain -- and the parent it was resized from is already corrected, which is fine: coverage
			// is what we are matching, not the alpha values themselves.
			if (alphaCutoff)
			{
				matchAlphaCoverage(
					std::span<std::byte>(out.pixels.data() + offset, slice),
					*alphaCutoff,
					targetCoverage);
			}

			out.subresources.push_back({ offset, static_cast<uint64_t>(mw) * 4, slice });

			prevOffset = offset;
			prevW      = mw;
			prevH      = mh;
			offset += slice;
		}

		return out;
	}
}
