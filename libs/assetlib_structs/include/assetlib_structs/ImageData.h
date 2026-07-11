#pragma once
#include <assetlib_structs/VkFormat.h>
#include <core/containers/fixed_buffer.h>

namespace assetlib
{
	struct ImageSubresource
	{
		size_t   offset     = 0;  // byte offset into ImageData::pixels
		uint64_t rowPitch   = 0;  // bytes between rows
		uint64_t slicePitch = 0;  // bytes of the whole 2D subresource
	};

	/**
	 * Represents a move-only raw image data with
	 * optional mipmaps and array layers.
	 */
	struct ImageData
	{
		uint32_t width     = 0;
		uint32_t height    = 0;
		uint32_t mipLevels = 1;
		uint32_t arraySize = 1;
		// The format tag the KTX2 container carries natively. API-neutral on purpose: each RHI
		// backend maps it to its own format (see the D3D12 VkFormatToDXGI path). Not a DXGI/Metal
		// format.
		VkFormat vkFormat  = VkFormat::Undefined;
		bool     isCubemap = false;

		core::fixed_buffer<std::byte> pixels;
		std::vector<ImageSubresource> subresources;

		ImageData()                     = default;
		ImageData(ImageData&&) noexcept = default;
		ImageData(const ImageData&)     = delete;
		ImageData&
		operator=(ImageData&&) noexcept = default;
		ImageData&
		operator=(const ImageData&) = delete;
	};
}
