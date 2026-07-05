#include <assetlib/image_io.h>

#ifdef _WIN32
#	include <DirectXTex.h>
#endif

namespace assetlib
{
#ifdef _WIN32
	ImageData
	loadDDS(const std::filesystem::path& path)
	{
		DirectX::TexMetadata  metadata = {};
		DirectX::ScratchImage scratch;
		const HRESULT         hr =
			DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, scratch);
		if (FAILED(hr))
		{
			throw std::runtime_error("assetlib::loadDDS: failed to load '" + path.string() + "'");
		}

		ImageData image;
		image.width      = static_cast<uint32_t>(metadata.width);
		image.height     = static_cast<uint32_t>(metadata.height);
		image.mipLevels  = static_cast<uint32_t>(metadata.mipLevels);
		image.arraySize  = static_cast<uint32_t>(metadata.arraySize);
		image.dxgiFormat = static_cast<uint32_t>(metadata.format);
		image.isCubemap  = metadata.IsCubemap();

		// DirectXTex orders images as arraySlice-major, mip-minor -- the same order D3D12
		// uses for subresources -- so pack them contiguously in that order.
		const DirectX::Image* images     = scratch.GetImages();
		const size_t          imageCount = scratch.GetImageCount();

		size_t totalBytes = 0;
		for (size_t i = 0; i < imageCount; ++i)
		{
			totalBytes += images[i].slicePitch;
		}

		image.pixels = core::fixed_buffer<std::byte>(totalBytes);
		image.subresources.reserve(imageCount);

		size_t offset = 0;
		for (size_t i = 0; i < imageCount; ++i)
		{
			std::memcpy(image.pixels.data() + offset, images[i].pixels, images[i].slicePitch);
			image.subresources.push_back({ offset, images[i].rowPitch, images[i].slicePitch });
			offset += images[i].slicePitch;
		}

		return image;
	}

	void
	writeDDS(const ImageData& image, const std::filesystem::path& path)
	{
		DirectX::TexMetadata meta{};
		meta.width      = image.width;
		meta.height     = image.height;
		meta.depth      = 1;
		meta.arraySize  = image.arraySize;
		meta.mipLevels  = image.mipLevels;
		meta.format     = static_cast<DXGI_FORMAT>(image.dxgiFormat);
		meta.dimension  = DirectX::TEX_DIMENSION_TEXTURE2D;
		meta.miscFlags  = image.isCubemap ? DirectX::TEX_MISC_TEXTURECUBE : 0u;
		meta.miscFlags2 = 0;

		// Rebuild the DirectXTex image view in its native arraySlice-major, mip-minor order -- the same
		// order loadDDS packed the subresources in. Per-mip dimensions halve from the base.
		std::vector<DirectX::Image> images;
		images.reserve(image.subresources.size());
		for (uint32_t item = 0; item < image.arraySize; ++item)
		{
			for (uint32_t mip = 0; mip < image.mipLevels; ++mip)
			{
				const size_t index = static_cast<size_t>(item) * image.mipLevels + mip;
				if (index >= image.subresources.size())
					throw std::runtime_error("assetlib::writeDDS: subresource count mismatch");

				const auto& sub = image.subresources[index];

				DirectX::Image img{};
				// Parenthesised to dodge the windows.h max() macro (DirectXTex pulls it in).
				img.width      = (std::max)(1u, image.width >> mip);
				img.height     = (std::max)(1u, image.height >> mip);
				img.format     = meta.format;
				img.rowPitch   = sub.rowPitch;
				img.slicePitch = sub.slicePitch;
				img.pixels     = reinterpret_cast<uint8_t*>(
					const_cast<std::byte*>(image.pixels.data()) + sub.offset);
				images.push_back(img);
			}
		}

		const HRESULT hr = DirectX::SaveToDDSFile(
			images.data(),
			images.size(),
			meta,
			DirectX::DDS_FLAGS_NONE,
			path.c_str());
		if (FAILED(hr))
			throw std::runtime_error("assetlib::writeDDS: failed to write '" + path.string() + "'");
	}
#else
	ImageData
	loadDDS(const std::filesystem::path&)
	{
		throw std::runtime_error(
			"assetlib::loadDDS: DDS support requires DirectXTex (Windows only)");
	}

	void
	writeDDS(const ImageData&, const std::filesystem::path&)
	{
		throw std::runtime_error(
			"assetlib::writeDDS: DDS support requires DirectXTex (Windows only)");
	}
#endif
}
