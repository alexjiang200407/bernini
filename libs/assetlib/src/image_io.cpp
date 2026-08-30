#include <assetlib/image_io.h>
#include <assetlib_structs/ImageData.h>
#include <core/math.h>
#include <core/platform/util.h>

#include <ktx.h>

#include "mounted_io.h"

#include <tracy/Tracy.hpp>

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
			case VkFormat::E5B9G9R9_UFLOAT_PACK32:
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
			case VkFormat::UNDEFINED:
			default:
				throw std::runtime_error(
					"assetlib: no block info for Vulkan format " +
					std::to_string(static_cast<uint32_t>(vk)));
			}
		}

		void
		check(ktx_error_code_e code, const char* what, const std::filesystem::path& path)
		{
			if (code == KTX_SUCCESS)
				return;

			auto message = std::string(what) + " '" + path.string() + "': " + ktxErrorString(code);

			const bool fileError = code == KTX_FILE_OPEN_FAILED || code == KTX_FILE_WRITE_ERROR ||
			                       code == KTX_FILE_READ_ERROR;
			if (fileError && errno != 0)
				message += " (" + std::generic_category().message(errno) + ")";

			throw std::runtime_error(message);
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

		// libktx builds its Basis codec tables on first use, guarded by a plain non-atomic
		// `static bool` -- `transcoderInitialized` in lib/basis_transcode.cpp and
		// `basisuEncoderInitialized` in lib/basis_encode.cpp. Two threads reaching their first
		// transcode (or encode) together therefore race: both may run the init, or one may observe
		// the flag set while the other is still filling the tables it is about to read.
		//
		// The editor decodes texture previews on a worker pool while the UI thread loads the same
		// textures for the GPU, so this is reachable. Serialize until a call has completed the
		// init; afterwards both codecs are re-entrant across distinct ktxTexture2 instances and we
		// take no lock. The two share one mutex because basisu's encoder init also touches the
		// transcoder tables.
		std::mutex&
		basisInitMutex()
		{
			static std::mutex g_Mutex;
			return g_Mutex;
		}

		ktx_error_code_e
		transcodeBasis(ktxTexture2* texture, ktx_transcode_fmt_e format)
		{
			static std::atomic<bool> g_Ready{ false };

			if (g_Ready.load(std::memory_order_acquire))
				return ktxTexture2_TranscodeBasis(texture, format, 0);

			const std::lock_guard<std::mutex> lock(basisInitMutex());

			const ktx_error_code_e rc = ktxTexture2_TranscodeBasis(texture, format, 0);
			if (rc == KTX_SUCCESS)
				g_Ready.store(true, std::memory_order_release);
			return rc;
		}

		ktx_error_code_e
		compressBasis(ktxTexture2* texture, ktxBasisParams* params)
		{
			static std::atomic<bool> g_Ready{ false };

			if (g_Ready.load(std::memory_order_acquire))
				return ktxTexture2_CompressBasisEx(texture, params);

			const std::lock_guard<std::mutex> lock(basisInitMutex());

			const ktx_error_code_e rc = ktxTexture2_CompressBasisEx(texture, params);
			if (rc == KTX_SUCCESS)
				g_Ready.store(true, std::memory_order_release);
			return rc;
		}

		bool
		isRgba8(VkFormat vk)
		{
			return vk == VkFormat::R8G8B8A8_UNORM || vk == VkFormat::R8G8B8A8_SRGB;
		}

		bool
		isBgra8(VkFormat vk)
		{
			return vk == VkFormat::B8G8R8A8_UNORM || vk == VkFormat::B8G8R8A8_SRGB;
		}

		// The smallest mip whose longer edge still covers `maxDim`, so the consumer only ever
		// scales down. Falls back to the base mip when the whole image is already smaller.
		uint32_t
		previewMipLevel(uint32_t width, uint32_t height, uint32_t levels, uint32_t maxDim)
		{
			uint32_t mip = 0;
			while (mip + 1 < levels)
			{
				const uint32_t nextLonger = (std::max)(1u, (std::max)(width, height) >> (mip + 1));
				if (nextLonger < maxDim)
					break;
				++mip;
			}
			return mip;
		}

		// loadKTX2 destroys its texture by hand, which leaks whenever a later step throws. The
		// preview path has several such steps, so it owns the handle instead.
		struct Ktx2Owner
		{
			ktxTexture2* tex = nullptr;

			~Ktx2Owner()
			{
				if (tex != nullptr)
					ktxTexture_Destroy(ktxTexture(tex));
			}
		};
	}

	// Everything loadKTX2 and decodeKTX2 share once the container is open, so a file and an embedded
	// blob cannot decode differently. `path` names the source for error messages only.
	static ImageData
	imageFromKtx(
		ktxTexture2*                 texture,
		Ktx2Decode                   decode,
		uint32_t                     maxDim,
		const std::filesystem::path& path)
	{
		ZoneScopedN("assetlib ktx2 decode");
		ZoneTextF(
			"%s, %ux%u, %u levels",
			path.filename().string().c_str(),
			texture->baseWidth,
			texture->baseHeight,
			texture->numLevels);

		// Basis-supercompressed textures (LDR material maps) transcode on the way in. The GPU wants a
		// block format; the material bake wants texels it can composite, so it asks for RGBA32 instead.
		// HDR / IBL maps are stored uncompressed and skip this entirely.
		if (ktxTexture2_NeedsTranscoding(texture))
		{
			const ktx_transcode_fmt_e target =
				decode == Ktx2Decode::kRgba8 ? KTX_TTF_RGBA32 : KTX_TTF_BC7_RGBA;

			const ktx_error_code_e tc = transcodeBasis(texture, target);
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

		const BlockInfo block = blockInfo(static_cast<VkFormat>(texture->vkFormat));

		// D3D12 requires a block-compressed texture's most-detailed level to be block-aligned, so
		// the tail may not start on a mip a block does not divide.
		uint32_t firstMip = 0;
		if (maxDim > 0)
		{
			firstMip = previewMipLevel(
				texture->baseWidth,
				texture->baseHeight,
				texture->numLevels,
				maxDim);
			while (firstMip > 0 &&
			       (((std::max)(1u, texture->baseWidth >> firstMip) % block.width) != 0 ||
			        ((std::max)(1u, texture->baseHeight >> firstMip) % block.height) != 0))
				--firstMip;
		}

		ImageData image;
		image.width          = (std::max)(1u, texture->baseWidth >> firstMip);
		image.height         = (std::max)(1u, texture->baseHeight >> firstMip);
		image.mipLevels      = texture->numLevels - firstMip;
		image.isCubemap      = texture->isCubemap;
		const uint32_t faces = texture->isCubemap ? 6u : 1u;
		image.arraySize      = texture->numLayers * faces;
		image.vkFormat =
			static_cast<VkFormat>(texture->vkFormat);  // BC7 after transcode, else as-is

		ktxTexture* base = ktxTexture(texture);

		// Sum every face/mip so we can pack them contiguously in D3D12 subresource order
		// (all mips of face 0, then face 1, ...).
		size_t totalBytes = 0;
		for (uint32_t layer = 0; layer < texture->numLayers; ++layer)
			for (uint32_t face = 0; face < faces; ++face)
				for (uint32_t mip = firstMip; mip < texture->numLevels; ++mip)
					totalBytes += ktxTexture_GetImageSize(base, mip);

		image.pixels = core::fixed_buffer<std::byte>(totalBytes);
		image.subresources.reserve(image.arraySize * image.mipLevels);

		size_t dstOffset = 0;
		for (uint32_t layer = 0; layer < texture->numLayers; ++layer)
		{
			for (uint32_t face = 0; face < faces; ++face)
			{
				for (uint32_t mip = firstMip; mip < texture->numLevels; ++mip)
				{
					ktx_size_t srcOffset = 0;
					check(
						ktxTexture_GetImageOffset(base, mip, layer, face, &srcOffset),
						"assetlib::loadKTX2: bad image offset in",
						path);

					const size_t   size = ktxTexture_GetImageSize(base, mip);
					const uint32_t mipW = (std::max)(1u, texture->baseWidth >> mip);
					const uint64_t pitch =
						static_cast<uint64_t>(core::div_ceil(mipW, block.width)) * block.bytes;

					std::memcpy(image.pixels.data() + dstOffset, base->pData + srcOffset, size);
					image.subresources.push_back({ dstOffset, pitch, size });
					dstOffset += size;
				}
			}
		}

		ktxTexture_Destroy(base);
		return image;
	}

	// Returns an owned texture the caller must destroy; `path` names the source for error messages
	// only. libktx copies the payload under LOAD_IMAGE_DATA_BIT, so `bytes` need not outlive this.
	static ktxTexture2*
	openKtxFromMemory(
		std::span<const std::byte>   bytes,
		const char*                  what,
		const std::filesystem::path& path)
	{
		ktxTexture2* texture = nullptr;

		check(
			ktxTexture2_CreateFromMemory(
				reinterpret_cast<const ktx_uint8_t*>(bytes.data()),
				bytes.size(),
				KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
				&texture),
			what,
			path);

		return texture;
	}

	ImageData
	loadKTX2(const std::filesystem::path& path, Ktx2Decode decode, uint32_t maxDim)
	{
		ktxTexture2* texture = nullptr;

		errno                     = 0;  // so check() reads this call's reason, not a stale one
		const ktx_error_code_e rc = ktxTexture2_CreateFromNamedFile(
			path.string().c_str(),
			KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
			&texture);
		check(rc, "assetlib::loadKTX2: failed to load", path);

		return imageFromKtx(texture, decode, maxDim, path);
	}

	ImageData
	loadKTX2(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		Ktx2Decode                     decode,
		uint32_t                       maxDim)
	{
		const std::vector<std::byte> bytes = fileSystem.Read(path);
		return imageFromKtx(
			openKtxFromMemory(bytes, "assetlib::loadKTX2: failed to read", path),
			decode,
			maxDim,
			path);
	}

	ImageData
	decodeKTX2(std::span<const std::byte> bytes, Ktx2Decode decode)
	{
		return imageFromKtx(
			openKtxFromMemory(bytes, "assetlib::decodeKTX2: failed to read", "<memory>"),
			decode,
			0,
			"<memory>");
	}

	// Everything the two loadKTX2Preview overloads share once the container is open, so a file and a
	// mounted entry cannot preview differently. `path` names the source for error messages only.
	static ImageData
	previewFromKtx(Ktx2Owner& owner, uint32_t maxDim, const std::filesystem::path& path)
	{
		// loadKTX2 asks the same Basis payload for BC7 because that is what the GPU samples. A CPU
		// consumer cannot read a block, so ask for RGBA8 instead.
		if (ktxTexture2_NeedsTranscoding(owner.tex))
		{
			check(
				transcodeBasis(owner.tex, KTX_TTF_RGBA32),
				"assetlib::loadKTX2Preview: failed to transcode",
				path);
		}

		const auto stored = static_cast<VkFormat>(owner.tex->vkFormat);
		if (!isRgba8(stored) && !isBgra8(stored))
		{
			// An HDR float map, or a block format written by some other tool: neither carries a
			// Basis payload to transcode, and we have no block decoder.
			throw std::runtime_error(
				"assetlib::loadKTX2Preview: '" + path.string() +
				"' has no CPU decode path for Vulkan format " +
				std::to_string(static_cast<uint32_t>(stored)));
		}

		ktxTexture*    base = ktxTexture(owner.tex);
		const uint32_t mip  = previewMipLevel(
			owner.tex->baseWidth,
			owner.tex->baseHeight,
			owner.tex->numLevels,
			maxDim);

		ktx_size_t srcOffset = 0;
		check(
			ktxTexture_GetImageOffset(base, mip, 0, 0, &srcOffset),
			"assetlib::loadKTX2Preview: bad image offset in",
			path);

		ImageData image;
		image.width     = (std::max)(1u, owner.tex->baseWidth >> mip);
		image.height    = (std::max)(1u, owner.tex->baseHeight >> mip);
		image.mipLevels = 1;
		image.arraySize = 1;
		image.isCubemap = false;
		image.vkFormat  = (stored == VkFormat::B8G8R8A8_SRGB || stored == VkFormat::R8G8B8A8_SRGB) ?
		                      VkFormat::R8G8B8A8_SRGB :
		                      VkFormat::R8G8B8A8_UNORM;

		const uint64_t dstPitch   = static_cast<uint64_t>(image.width) * 4;
		const size_t   totalBytes = static_cast<size_t>(dstPitch) * image.height;
		image.pixels              = core::fixed_buffer<std::byte>(totalBytes);

		// KTX pads rows to 4 bytes, which an RGBA8 row already satisfies -- but copy row by row
		// against the reported pitch rather than assume it.
		const ktx_size_t srcPitch = ktxTexture_GetRowPitch(base, mip);
		for (uint32_t y = 0; y < image.height; ++y)
		{
			std::memcpy(
				image.pixels.data() + y * dstPitch,
				base->pData + srcOffset + y * srcPitch,
				static_cast<size_t>(dstPitch));
		}

		if (isBgra8(stored))
		{
			auto* p = reinterpret_cast<uint8_t*>(image.pixels.data());
			for (size_t i = 0; i < totalBytes; i += 4) std::swap(p[i], p[i + 2]);
		}

		image.subresources.push_back({ 0, dstPitch, totalBytes });
		return image;
	}

	ImageData
	loadKTX2Preview(const std::filesystem::path& path, uint32_t maxDim)
	{
		if (maxDim == 0)
			throw std::runtime_error("assetlib::loadKTX2Preview: maxDim must be non-zero");

		Ktx2Owner owner;

		errno = 0;  // so check() reads this call's reason, not a stale one
		check(
			ktxTexture2_CreateFromNamedFile(
				path.string().c_str(),
				KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
				&owner.tex),
			"assetlib::loadKTX2Preview: failed to load",
			path);

		return previewFromKtx(owner, maxDim, path);
	}

	ImageData
	loadKTX2Preview(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		uint32_t                       maxDim)
	{
		if (maxDim == 0)
			throw std::runtime_error("assetlib::loadKTX2Preview: maxDim must be non-zero");

		const std::vector<std::byte> bytes = fileSystem.Read(path);

		Ktx2Owner owner;
		owner.tex = openKtxFromMemory(bytes, "assetlib::loadKTX2Preview: failed to read", path);

		return previewFromKtx(owner, maxDim, path);
	}

	// Everything writeKTX2 and encodeKTX2 share up to the point of emitting bytes. Returns an owned
	// texture the caller must destroy; `path` names the destination for error messages only.
	static ktxTexture2*
	buildKtx(
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

			const ktx_error_code_e cc = compressBasis(texture, &bp);
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
				const ktx_error_code_e tc = transcodeBasis(texture, transcodeTarget(compression));
				if (tc != KTX_SUCCESS)
				{
					ktxTexture_Destroy(base);
					check(tc, "assetlib::writeKTX2: failed to transcode", path);
				}
			}
		}

		return texture;
	}

	void
	writeKTX2(
		const ImageData&             image,
		const std::filesystem::path& path,
		bool                         srgb,
		Ktx2Compression              compression)
	{
		ktxTexture* base = ktxTexture(buildKtx(image, path, srgb, compression));

		// Written beside the target and renamed over it, like core::file::write_atomic, because a
		// baked map's name is content-addressed: two materials routing the same sources at the same
		// settings resolve to one path, and a threaded bake can have both find it absent and write
		// it at once. Identical bytes, so whichever rename lands last is right -- but two writers
		// in one open file are not. The name carries the process and a counter so the temps
		// themselves cannot collide.
		static std::atomic<uint32_t> g_Counter = 0;

		const std::filesystem::path tmp = std::filesystem::path(
			std::format(
				"{}.{}.{}.tmp",
				path.string(),
				core::process_id(),
				g_Counter.fetch_add(1, std::memory_order_relaxed)));

		errno                         = 0;
		const ktx_error_code_e wc     = ktxTexture_WriteToNamedFile(base, tmp.string().c_str());
		const int              reason = errno;

		ktxTexture_Destroy(base);

		if (wc != KTX_SUCCESS)
		{
			std::error_code ec;
			std::filesystem::remove(tmp, ec);

			// Restored after the removal, not before it: a remove that fails too would leave its
			// own errno for check() to blame the write's failure on.
			errno = reason;
			check(wc, "assetlib::writeKTX2: failed to write", tmp);
		}

		// The rename below is what stops a *reader* ever seeing a partial file. This is what stops
		// a crash leaving one: a rename can be durable before the data pages behind it are, and
		// what that leaves is a complete-looking file that reads back as garbage. It matters more
		// here than for a cache that can shrug it off, because the up-to-date test is
		// `hasBytes` -- a stat -- so such a file is taken for a finished bake and never rebuilt.
		if (!core::sync_file(tmp))
		{
			std::error_code ec;
			std::filesystem::remove(tmp, ec);
			throw std::runtime_error(
				std::format("assetlib::writeKTX2: cannot flush '{}'", tmp.string()));
		}

		std::error_code ec;
		std::filesystem::rename(tmp, path, ec);
		if (ec)
		{
			std::error_code removeEc;
			std::filesystem::remove(tmp, removeEc);
			throw std::runtime_error(
				std::format(
					"assetlib::writeKTX2: cannot commit '{}': {}",
					path.string(),
					ec.message()));
		}

		// Durable bytes under a directory entry that is not durable is still a lost write, which
		// here reads as a map that vanished. Not fatal: the rename has happened, and what is at
		// risk is only its ordering against an unrelated crash. Same pairing as write_atomic.
		(void)core::sync_directory(path.parent_path());
	}

	ImageData
	packRgb9e5(const ImageData& image)
	{
		if (image.vkFormat != VkFormat::R32G32B32A32_SFLOAT)
			throw std::runtime_error("assetlib::packRgb9e5: source must be R32G32B32A32_SFLOAT");

		// GL_EXT_texture_shared_exponent: 9 mantissa bits, exponent bias 15, 5-bit exponent.
		constexpr int   c_Mantissa = 9;
		constexpr int   c_Bias     = 15;
		constexpr int   c_MaxExp   = 31;
		constexpr float c_Max      = static_cast<float>((1 << c_Mantissa) - 1) /
		                             static_cast<float>(1 << c_Mantissa) *
		                             static_cast<float>(1 << (c_MaxExp - c_Bias));

		ImageData out;
		out.width     = image.width;
		out.height    = image.height;
		out.mipLevels = image.mipLevels;
		out.arraySize = image.arraySize;
		out.isCubemap = image.isCubemap;
		out.vkFormat  = VkFormat::E5B9G9R9_UFLOAT_PACK32;

		size_t texels = 0;
		for (const ImageSubresource& sub : image.subresources)
			texels += static_cast<size_t>(sub.slicePitch) / (sizeof(float) * 4);

		out.pixels = core::fixed_buffer<std::byte>(texels * sizeof(uint32_t));

		size_t dst = 0;
		for (const ImageSubresource& sub : image.subresources)
		{
			const auto count = static_cast<size_t>(sub.slicePitch) / (sizeof(float) * 4);
			const auto rows =
				sub.rowPitch > 0 ? static_cast<size_t>(sub.slicePitch / sub.rowPitch) : 1u;
			const size_t width = rows > 0 ? count / rows : count;

			out.subresources.push_back(
				{ dst * sizeof(uint32_t),
			      static_cast<uint64_t>(width) * sizeof(uint32_t),
			      static_cast<uint64_t>(count) * sizeof(uint32_t) });

			const auto* src    = reinterpret_cast<const float*>(image.pixels.data() + sub.offset);
			auto*       target = reinterpret_cast<uint32_t*>(out.pixels.data()) + dst;

			for (size_t t = 0; t < count; ++t)
			{
				const float r = std::clamp(src[t * 4 + 0], 0.0f, c_Max);
				const float g = std::clamp(src[t * 4 + 1], 0.0f, c_Max);
				const float b = std::clamp(src[t * 4 + 2], 0.0f, c_Max);

				const float maxc = std::max(std::max(r, g), b);

				int exp = maxc > 0.0f ?
				              std::max(-c_Bias - 1, static_cast<int>(std::floor(std::log2(maxc)))) +
				                  1 + c_Bias :
				              0;

				const auto quantize = [&](float v, int e) {
					return static_cast<int>(std::floor(
						static_cast<double>(v) /
							std::exp2(static_cast<double>(e - c_Bias - c_Mantissa)) +
						0.5));
				};

				// Rounding the brightest channel can carry it into the next exponent.
				if (quantize(maxc, exp) == (1 << c_Mantissa))
					++exp;

				exp = std::clamp(exp, 0, c_MaxExp);

				const auto rs =
					static_cast<uint32_t>(std::clamp(quantize(r, exp), 0, (1 << c_Mantissa) - 1));
				const auto gs =
					static_cast<uint32_t>(std::clamp(quantize(g, exp), 0, (1 << c_Mantissa) - 1));
				const auto bs =
					static_cast<uint32_t>(std::clamp(quantize(b, exp), 0, (1 << c_Mantissa) - 1));

				target[t] = rs | (gs << 9) | (bs << 18) | (static_cast<uint32_t>(exp) << 27);
			}

			dst += count;
		}

		return out;
	}

	ImageData
	unpackRgb9e5(const ImageData& image)
	{
		if (image.vkFormat != VkFormat::E5B9G9R9_UFLOAT_PACK32)
			throw std::runtime_error(
				"assetlib::unpackRgb9e5: source must be E5B9G9R9_UFLOAT_PACK32");

		constexpr int c_Mantissa = 9;
		constexpr int c_Bias     = 15;

		ImageData out;
		out.width     = image.width;
		out.height    = image.height;
		out.mipLevels = image.mipLevels;
		out.arraySize = image.arraySize;
		out.isCubemap = image.isCubemap;
		out.vkFormat  = VkFormat::R32G32B32A32_SFLOAT;

		size_t texels = 0;
		for (const ImageSubresource& sub : image.subresources)
			texels += static_cast<size_t>(sub.slicePitch) / sizeof(uint32_t);

		out.pixels = core::fixed_buffer<std::byte>(texels * sizeof(float) * 4);

		size_t dst = 0;
		for (const ImageSubresource& sub : image.subresources)
		{
			const auto count = static_cast<size_t>(sub.slicePitch) / sizeof(uint32_t);
			const auto rows =
				sub.rowPitch > 0 ? static_cast<size_t>(sub.slicePitch / sub.rowPitch) : 1u;
			const size_t width = rows > 0 ? count / rows : count;

			out.subresources.push_back(
				{ dst * sizeof(float) * 4,
			      static_cast<uint64_t>(width) * sizeof(float) * 4,
			      static_cast<uint64_t>(count) * sizeof(float) * 4 });

			const auto* src = reinterpret_cast<const uint32_t*>(image.pixels.data() + sub.offset);
			auto*       target = reinterpret_cast<float*>(out.pixels.data()) + dst * 4;

			for (size_t t = 0; t < count; ++t)
			{
				const uint32_t packed = src[t];
				const auto     exp    = static_cast<int>(packed >> 27);
				const float    scale =
					static_cast<float>(std::exp2(static_cast<double>(exp - c_Bias - c_Mantissa)));

				target[t * 4 + 0] = static_cast<float>(packed & 0x1FFu) * scale;
				target[t * 4 + 1] = static_cast<float>((packed >> 9) & 0x1FFu) * scale;
				target[t * 4 + 2] = static_cast<float>((packed >> 18) & 0x1FFu) * scale;
				target[t * 4 + 3] = 1.0f;
			}

			dst += count;
		}

		return out;
	}

	std::vector<std::byte>
	encodeKTX2(const ImageData& image, bool srgb, Ktx2Compression compression)
	{
		// Written through a stream backed by our own vector rather than through
		// ktxTexture_WriteToMemory. That returns a buffer libktx allocated inside ktx.dll, and
		// releasing it here would free a pointer belonging to another CRT's heap -- which trips
		// _CrtIsValidHeapPointer on a debug build and corrupts the heap on a release one.
		auto out = std::vector<std::byte>();

		ktxStream stream{};
		stream.type                    = eStreamTypeCustom;
		stream.data.custom_ptr.address = &out;
		stream.closeOnDestruct         = KTX_FALSE;

		stream.write = [](ktxStream* str, const void* src, ktx_size_t size, ktx_size_t count) {
			auto*        sink  = static_cast<std::vector<std::byte>*>(str->data.custom_ptr.address);
			const size_t bytes = size * count;
			const auto*  from  = static_cast<const std::byte*>(src);

			const size_t at = static_cast<size_t>(str->readpos);
			if (at + bytes > sink->size())
				sink->resize(at + bytes);
			std::memcpy(sink->data() + at, from, bytes);
			str->readpos = static_cast<ktx_off_t>(at + bytes);
			return KTX_SUCCESS;
		};
		stream.getpos = [](ktxStream* str, ktx_off_t* offset) {
			*offset = str->readpos;
			return KTX_SUCCESS;
		};
		stream.setpos = [](ktxStream* str, ktx_off_t offset) {
			str->readpos = offset;
			return KTX_SUCCESS;
		};
		stream.getsize = [](ktxStream* str, ktx_size_t* size) {
			*size = static_cast<std::vector<std::byte>*>(str->data.custom_ptr.address)->size();
			return KTX_SUCCESS;
		};
		stream.read     = [](ktxStream*, void*, ktx_size_t) { return KTX_INVALID_OPERATION; };
		stream.skip     = [](ktxStream*, ktx_size_t) { return KTX_INVALID_OPERATION; };
		stream.destruct = [](ktxStream*) {};

		ktxTexture* base = ktxTexture(buildKtx(image, "<memory>", srgb, compression));

		const ktx_error_code_e wc = ktxTexture_WriteToStream(base, &stream);
		ktxTexture_Destroy(base);
		check(wc, "assetlib::encodeKTX2: failed to encode", "<memory>");

		return out;
	}
}
