#pragma once
#include <core/containers/fixed_buffer.h>

namespace assetlib
{
	// One mip/array subresource within ImageData::pixels. Ordered as D3D12 expects
	// subresources: all mips of array slice 0, then slice 1, ... (for a mipped cube
	// that is 6 * mipLevels entries).
	struct ImageSubresource
	{
		size_t   offset     = 0;  // byte offset into ImageData::pixels
		uint64_t rowPitch   = 0;  // bytes between rows
		uint64_t slicePitch = 0;  // bytes of the whole 2D subresource
	};

	// A decoded, GPU-uploadable image. This is the neutral hand-off type between asset
	// code (which decodes files -- see assetlib::loadDDS) and graphics code (which turns
	// it into a GPU texture). It lives in the lightweight assetlib_structs target so the
	// loaders (assetlib) and the RHI (bgl) can share it without depending on each other's
	// implementation. The pixel format is carried as a raw DXGI_FORMAT value; no DXGI type
	// is exposed.
	struct ImageData
	{
		uint32_t width      = 0;
		uint32_t height     = 0;
		uint32_t mipLevels  = 1;
		uint32_t arraySize  = 1;
		uint32_t dxgiFormat = 0;  // raw DXGI_FORMAT value
		bool     isCubemap  = false;

		core::fixed_buffer<std::byte>  pixels;
		std::vector<ImageSubresource>  subresources;

		// Move-only (owns a unique pixel buffer). Declared explicitly so the deleted copy
		// is intentional rather than implicit (silences C4625/C4626 under strict warnings).
		ImageData()                     = default;
		ImageData(ImageData&&) noexcept = default;
		ImageData(const ImageData&)     = delete;
		ImageData&
		operator=(ImageData&&) noexcept = default;
		ImageData&
		operator=(const ImageData&) = delete;
	};
}
