#include <assetlib/image_io.h>

#include <ktx.h>

namespace assetlib
{
	namespace
	{
		// The sRGB sibling of a linear format (mirrors DirectX::MakeSRGB). Returns the input
		// unchanged when it has no sRGB variant or is already sRGB.
		VkFormat
		toSrgbVk(VkFormat vk)
		{
			if (vk == VkFormat::R8G8B8A8_UNORM)
				return VkFormat::R8G8B8A8_SRGB;
			if (vk == VkFormat::B8G8R8A8_UNORM)
				return VkFormat::B8G8R8A8_SRGB;
			return vk;
		}

		// The 8-bit LDR uncompressed formats we can hand to the Basis encoder. HDR / float formats
		// (the IBL maps) are left uncompressed -- Basis Universal is LDR-only.
		bool
		isBasisCompressible(VkFormat vk)
		{
			return vk == VkFormat::R8G8B8A8_UNORM || vk == VkFormat::R8G8B8A8_SRGB ||
			       vk == VkFormat::B8G8R8A8_UNORM || vk == VkFormat::B8G8R8A8_SRGB;
		}

		struct BlockInfo
		{
			uint32_t width;   // pixels per block edge (1 for uncompressed)
			uint32_t height;  //
			uint32_t bytes;   // bytes per block (per texel for uncompressed)
		};

		// Texel-block geometry of a format, used to compute a subresource's row pitch. BC formats
		// are 4x4 blocks; uncompressed formats are 1x1 "blocks" of one texel.
		BlockInfo
		blockInfo(VkFormat vk)
		{
			switch (vk)
			{
			case VkFormat::R8_UNORM:
				return { 1, 1, 1 };
			case VkFormat::R8G8_UNORM:
				return { 1, 1, 2 };
			case VkFormat::R8G8B8A8_UNORM:
			case VkFormat::R8G8B8A8_SRGB:
			case VkFormat::B8G8R8A8_UNORM:
			case VkFormat::B8G8R8A8_SRGB:
			case VkFormat::R16G16_UNORM:
			case VkFormat::R16G16_SFLOAT:
			case VkFormat::R32_SFLOAT:
				return { 1, 1, 4 };
			case VkFormat::R16G16B16A16_UNORM:
			case VkFormat::R16G16B16A16_SFLOAT:
			case VkFormat::R32G32_SFLOAT:
				return { 1, 1, 8 };
			case VkFormat::R32G32B32A32_SFLOAT:
				return { 1, 1, 16 };
			case VkFormat::BC1_RGB_UNORM_BLOCK:
			case VkFormat::BC1_RGB_SRGB_BLOCK:
			case VkFormat::BC1_RGBA_UNORM_BLOCK:
			case VkFormat::BC1_RGBA_SRGB_BLOCK:
			case VkFormat::BC4_UNORM_BLOCK:
			case VkFormat::BC4_SNORM_BLOCK:
				return { 4, 4, 8 };
			case VkFormat::BC2_UNORM_BLOCK:
			case VkFormat::BC2_SRGB_BLOCK:
			case VkFormat::BC3_UNORM_BLOCK:
			case VkFormat::BC3_SRGB_BLOCK:
			case VkFormat::BC5_UNORM_BLOCK:
			case VkFormat::BC5_SNORM_BLOCK:
			case VkFormat::BC6H_UFLOAT_BLOCK:
			case VkFormat::BC6H_SFLOAT_BLOCK:
			case VkFormat::BC7_UNORM_BLOCK:
			case VkFormat::BC7_SRGB_BLOCK:
				return { 4, 4, 16 };
			case VkFormat::Undefined:
			default:
				throw std::runtime_error(
					"assetlib: no block info for Vulkan format " +
					std::to_string(static_cast<uint32_t>(vk)));
			}
		}

		void
		check(ktx_error_code_e code, const char* what, const std::filesystem::path& path)
		{
			if (code != KTX_SUCCESS)
				throw std::runtime_error(
					std::string(what) + " '" + path.string() + "': " + ktxErrorString(code));
		}

		// The block format a bake target transcodes to. kNone / kBasisUASTC never reach here.
		ktx_transcode_fmt_e
		transcodeTarget(Ktx2Compression compression)
		{
			switch (compression)
			{
			case Ktx2Compression::kBC1_RGB:
				return KTX_TTF_BC1_RGB;
			case Ktx2Compression::kBC5_RG:
				return KTX_TTF_BC5_RG;
			case Ktx2Compression::kBC7_RGBA:
				return KTX_TTF_BC7_RGBA;
			case Ktx2Compression::kNone:
			case Ktx2Compression::kBasisUASTC:
			default:
				throw std::runtime_error("assetlib::writeKTX2: not a block-compressed target");
			}
		}

		bool
		isBakeTarget(Ktx2Compression compression)
		{
			return compression == Ktx2Compression::kBC1_RGB ||
			       compression == Ktx2Compression::kBC5_RG ||
			       compression == Ktx2Compression::kBC7_RGBA;
		}
	}

	ImageData
	loadKTX2(const std::filesystem::path& path, Ktx2Decode decode)
	{
		ktxTexture2*           texture = nullptr;
		const ktx_error_code_e rc      = ktxTexture2_CreateFromNamedFile(
			path.string().c_str(),
			KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
			&texture);
		check(rc, "assetlib::loadKTX2: failed to load", path);

		// Basis-supercompressed textures (LDR material maps) transcode on the way in. The GPU wants a
		// block format; the material bake wants texels it can composite, so it asks for RGBA32 instead.
		// HDR / IBL maps are stored uncompressed and skip this entirely.
		if (ktxTexture2_NeedsTranscoding(texture))
		{
			const ktx_transcode_fmt_e target =
				decode == Ktx2Decode::kRgba8 ? KTX_TTF_RGBA32 : KTX_TTF_BC7_RGBA;

			const ktx_error_code_e tc = ktxTexture2_TranscodeBasis(texture, target, 0);
			if (tc != KTX_SUCCESS)
			{
				ktxTexture_Destroy(ktxTexture(texture));
				check(tc, "assetlib::loadKTX2: failed to transcode", path);
			}
		}
		else if (
			decode == Ktx2Decode::kRgba8 &&
			blockInfo(static_cast<VkFormat>(texture->vkFormat)).width != 1)
		{
			// An already-block-compressed file (a previously baked map, say). libktx will not decode
			// one, and guessing texels out of BC blocks is not this library's job.
			const VkFormat vk = static_cast<VkFormat>(texture->vkFormat);
			ktxTexture_Destroy(ktxTexture(texture));
			throw std::runtime_error(
				"assetlib::loadKTX2: cannot decode '" + path.string() +
				"' to RGBA8: it is already block-compressed (Vulkan format " +
				std::to_string(static_cast<uint32_t>(vk)) + ")");
		}

		ImageData image;
		image.width          = texture->baseWidth;
		image.height         = texture->baseHeight;
		image.mipLevels      = texture->numLevels;
		image.isCubemap      = texture->isCubemap;
		const uint32_t faces = texture->isCubemap ? 6u : 1u;
		image.arraySize      = texture->numLayers * faces;
		image.vkFormat =
			static_cast<VkFormat>(texture->vkFormat);  // BC7 after transcode, else as-is

		const BlockInfo block = blockInfo(image.vkFormat);

		ktxTexture* base = ktxTexture(texture);

		// Sum every face/mip so we can pack them contiguously in D3D12 subresource order
		// (all mips of face 0, then face 1, ...).
		size_t totalBytes = 0;
		for (uint32_t layer = 0; layer < texture->numLayers; ++layer)
			for (uint32_t face = 0; face < faces; ++face)
				for (uint32_t mip = 0; mip < texture->numLevels; ++mip)
					totalBytes += ktxTexture_GetImageSize(base, mip);

		image.pixels = core::fixed_buffer<std::byte>(totalBytes);
		image.subresources.reserve(image.arraySize * texture->numLevels);

		size_t dstOffset = 0;
		for (uint32_t layer = 0; layer < texture->numLayers; ++layer)
		{
			for (uint32_t face = 0; face < faces; ++face)
			{
				for (uint32_t mip = 0; mip < texture->numLevels; ++mip)
				{
					ktx_size_t srcOffset = 0;
					check(
						ktxTexture_GetImageOffset(base, mip, layer, face, &srcOffset),
						"assetlib::loadKTX2: bad image offset in",
						path);

					const size_t   size = ktxTexture_GetImageSize(base, mip);
					const uint32_t mipW = (std::max)(1u, image.width >> mip);
					const uint64_t pitch =
						static_cast<uint64_t>((mipW + block.width - 1) / block.width) * block.bytes;

					std::memcpy(image.pixels.data() + dstOffset, base->pData + srcOffset, size);
					image.subresources.push_back({ dstOffset, pitch, size });
					dstOffset += size;
				}
			}
		}

		ktxTexture_Destroy(base);
		return image;
	}

	void
	writeKTX2(
		const ImageData&             image,
		const std::filesystem::path& path,
		bool                         srgb,
		Ktx2Compression              compression)
	{
		const uint32_t faces  = image.isCubemap ? 6u : 1u;
		const uint32_t layers = (std::max)(1u, image.arraySize / faces);

		ktxTextureCreateInfo info{};
		info.vkFormat   = static_cast<uint32_t>(srgb ? toSrgbVk(image.vkFormat) : image.vkFormat);
		info.baseWidth  = image.width;
		info.baseHeight = image.height;
		info.baseDepth  = 1;
		info.numDimensions   = 2;
		info.numLevels       = image.mipLevels;
		info.numLayers       = layers;
		info.numFaces        = faces;
		info.isArray         = KTX_FALSE;
		info.generateMipmaps = KTX_FALSE;  // ImageData already carries the full chain

		ktxTexture2* texture = nullptr;
		check(
			ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture),
			"assetlib::writeKTX2: failed to create",
			path);

		ktxTexture* base = ktxTexture(texture);

		// ImageData subresources are ordered array/face-major, mip-minor; feed each to libktx by
		// its (level, layer, face) coordinate so the container stores them in KTX order.
		for (uint32_t layer = 0; layer < layers; ++layer)
		{
			for (uint32_t face = 0; face < faces; ++face)
			{
				for (uint32_t mip = 0; mip < image.mipLevels; ++mip)
				{
					const size_t index =
						(static_cast<size_t>(layer) * faces + face) * image.mipLevels + mip;
					if (index >= image.subresources.size())
					{
						ktxTexture_Destroy(base);
						throw std::runtime_error("assetlib::writeKTX2: subresource count mismatch");
					}

					const ImageSubresource& sub = image.subresources[index];
					check(
						ktxTexture_SetImageFromMemory(
							base,
							mip,
							layer,
							face,
							reinterpret_cast<const ktx_uint8_t*>(image.pixels.data() + sub.offset),
							sub.slicePitch),
						"assetlib::writeKTX2: failed to set image in",
						path);
				}
			}
		}

		// Every compressed path goes through UASTC first: libktx has no direct BC encoder, so a bake
		// target is UASTC-encoded and then transcoded into its block format. HDR / IBL float maps are
		// left uncompressed -- Basis Universal is LDR-only -- as are callers that ask for kNone.
		const bool wantsCompression = compression != Ktx2Compression::kNone;
		if (wantsCompression && isBasisCompressible(image.vkFormat))
		{
			// UASTC block encoding is deterministic regardless of thread count, so parallelise it --
			// single-threaded encoding of large (e.g. 4K) mip chains is prohibitively slow.
			const uint32_t hc = std::thread::hardware_concurrency();

			ktxBasisParams bp{};
			bp.structSize  = sizeof(bp);
			bp.uastc       = KTX_TRUE;
			bp.uastcFlags  = KTX_PACK_UASTC_LEVEL_FASTER;
			bp.threadCount = hc != 0 ? hc : 4;

			bp.normalMap = compression == Ktx2Compression::kBC5_RG ? KTX_TRUE : KTX_FALSE;

			const ktx_error_code_e cc = ktxTexture2_CompressBasisEx(texture, &bp);
			if (cc != KTX_SUCCESS)
			{
				ktxTexture_Destroy(base);
				check(cc, "assetlib::writeKTX2: failed to compress", path);
			}

			// Resolve UASTC down to a concrete block format. The file then carries that format
			// directly, so loadKTX2 sees a non-Basis texture and uploads it without transcoding --
			// which is the whole point of baking to a per-map format.
			if (isBakeTarget(compression))
			{
				const ktx_error_code_e tc =
					ktxTexture2_TranscodeBasis(texture, transcodeTarget(compression), 0);
				if (tc != KTX_SUCCESS)
				{
					ktxTexture_Destroy(base);
					check(tc, "assetlib::writeKTX2: failed to transcode", path);
				}
			}
		}

		const ktx_error_code_e wc = ktxTexture_WriteToNamedFile(base, path.string().c_str());
		ktxTexture_Destroy(base);
		check(wc, "assetlib::writeKTX2: failed to write", path);
	}
}
