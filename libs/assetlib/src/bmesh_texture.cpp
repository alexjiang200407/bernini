#include "bmesh_texture.h"

#ifdef _WIN32
#	include <DirectXTex.h>
#endif

namespace assetlib
{
#ifdef _WIN32
	ImageData
	rgba8ToImage(std::span<const std::byte> rgba, uint32_t width, uint32_t height)
	{
		const size_t expected = static_cast<size_t>(width) * height * 4;
		if (rgba.size() < expected)
			throw std::runtime_error("bmesh: RGBA buffer smaller than width*height*4");

		DirectX::Image image{};
		image.width      = width;
		image.height     = height;
		image.format     = DXGI_FORMAT_R8G8B8A8_UNORM;
		image.rowPitch   = static_cast<size_t>(width) * 4;
		image.slicePitch = expected;
		image.pixels =
			reinterpret_cast<uint8_t*>(const_cast<std::byte*>(rgba.data()));  // copied below

		DirectX::ScratchImage base;
		if (FAILED(base.InitializeFromImage(image)))
			throw std::runtime_error("bmesh: failed to initialize scratch image");

		// Mip generation is best-effort; fall back to the base image if it is unavailable.
		DirectX::ScratchImage        mipped;
		const DirectX::ScratchImage* chain = &base;
		if (SUCCEEDED(
				DirectX::GenerateMipMaps(
					*base.GetImage(0, 0, 0),
					DirectX::TEX_FILTER_DEFAULT,
					0,
					mipped)))
			chain = &mipped;

		const auto& meta = chain->GetMetadata();

		ImageData out;
		out.width      = static_cast<uint32_t>(meta.width);
		out.height     = static_cast<uint32_t>(meta.height);
		out.mipLevels  = static_cast<uint32_t>(meta.mipLevels);
		out.arraySize  = static_cast<uint32_t>(meta.arraySize);
		out.dxgiFormat = static_cast<uint32_t>(meta.format);
		out.isCubemap  = meta.IsCubemap();

		// DirectXTex orders images as arraySlice-major, mip-minor -- the same order D3D12 uses for
		// subresources -- so pack them contiguously in that order (mirrors loadDDS).
		const DirectX::Image* images     = chain->GetImages();
		const size_t          imageCount = chain->GetImageCount();

		size_t totalBytes = 0;
		for (size_t i = 0; i < imageCount; ++i) totalBytes += images[i].slicePitch;

		out.pixels = core::fixed_buffer<std::byte>(totalBytes);
		out.subresources.reserve(imageCount);

		size_t offset = 0;
		for (size_t i = 0; i < imageCount; ++i)
		{
			std::memcpy(out.pixels.data() + offset, images[i].pixels, images[i].slicePitch);
			out.subresources.push_back({ offset, images[i].rowPitch, images[i].slicePitch });
			offset += images[i].slicePitch;
		}

		return out;
	}
#else
	ImageData
	rgba8ToImage(std::span<const std::byte>, uint32_t, uint32_t)
	{
		throw std::runtime_error("bmesh: texture conversion requires DirectXTex (Windows only)");
	}
#endif
}
