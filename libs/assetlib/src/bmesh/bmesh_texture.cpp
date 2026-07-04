#include "bmesh_texture.h"

#ifdef _WIN32
#	include <DirectXTex.h>
#endif

namespace assetlib::bmesh
{
#ifdef _WIN32
	Texture
	rgba8ToDds(std::span<const std::byte> rgba, uint32_t width, uint32_t height, std::string name)
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

		DirectX::Blob blob;
		if (FAILED(
				DirectX::SaveToDDSMemory(
					chain->GetImages(),
					chain->GetImageCount(),
					chain->GetMetadata(),
					DirectX::DDS_FLAGS_NONE,
					blob)))
			throw std::runtime_error("bmesh: failed to encode DDS");

		const auto& meta = chain->GetMetadata();

		Texture texture;
		texture.name       = std::move(name);
		texture.width      = static_cast<uint32_t>(meta.width);
		texture.height     = static_cast<uint32_t>(meta.height);
		texture.mipLevels  = static_cast<uint32_t>(meta.mipLevels);
		texture.dxgiFormat = static_cast<uint32_t>(meta.format);

		const auto* bytes = reinterpret_cast<const std::byte*>(blob.GetBufferPointer());
		texture.dds.assign(bytes, bytes + blob.GetBufferSize());
		return texture;
	}
#else
	Texture
	rgba8ToDds(std::span<const std::byte>, uint32_t, uint32_t, std::string)
	{
		throw std::runtime_error("bmesh: texture conversion requires DirectXTex (Windows only)");
	}
#endif
}
