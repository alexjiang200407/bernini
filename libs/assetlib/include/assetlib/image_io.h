#pragma once
#include <assetlib_structs/ImageData.h>

namespace assetlib
{
	enum class Ktx2Compression : uint32_t
	{
		kNone,
		kBasisUASTC,
		kBC1_RGB,
		kBC5_RG,
		kBC7_RGBA,
	};

	enum class Ktx2Decode : uint32_t
	{
		kGpu,
		kRgba8,
	};

	/**
	 * Decodes a .ktx2 file (2D or cube map, with mips) into a GPU-uploadable ImageData.
	 *
	 * Image decoding lives in the asset library (alongside glTF image extraction); graphics
	 * code stays codec-free and just consumes the decoded ImageData through its
	 * texture-create path. The KTX2 container carries a Vulkan format that is translated to
	 * the raw DXGI format ImageData exposes.
	 *
	 * @param path Path to a .ktx2 file.
	 * @param decode What to transcode a Basis-supercompressed file to; ignored for uncompressed ones.
	 * @throws std::runtime_error if the file cannot be read, decoded, or carries an unmapped format.
	 */
	[[nodiscard]] ImageData
	loadKTX2(const std::filesystem::path& path, Ktx2Decode decode = Ktx2Decode::kGpu);

	/**
	 * Decodes a `.ktx2` into a small uncompressed RGBA8 image for CPU display, e.g. an editor
	 * thumbnail. Returns one tightly packed subresource: the first face of the mip closest to
	 * `maxDim`, so `mipLevels` and `arraySize` are always 1 and `isCubemap` is always false.
	 *
	 * loadKTX2 transcodes to BC7 for the GPU, and nothing on the CPU can read a BC7 block. This
	 * transcodes the same Basis payload to RGBA8 instead, so no block decoder is needed.
	 *
	 * The bytes are the stored values, so an sRGB source yields R8G8B8A8_SRGB and a linear one
	 * (normal / ORM) yields R8G8B8A8_UNORM. A viewer that treats both as sRGB draws linear data
	 * maps brighter than the renderer samples them; that is the raw channel content, which is
	 * what previewing a data map is for.
	 *
	 * @param path Path to a `.ktx2` file.
	 * @param maxDim Target length of the longer edge. The smallest mip at least this large wins,
	 *        or the base mip when the whole image is smaller.
	 * @throws std::runtime_error if the file cannot be read, or carries a format with no CPU
	 *         decode path: HDR float maps, and block-compressed images with no Basis payload.
	 */
	[[nodiscard]] ImageData
	loadKTX2Preview(const std::filesystem::path& path, uint32_t maxDim = 128);

	/**
	 * Encodes an ImageData (its mips and array/cube faces) into a `.ktx2` file on disk. The inverse of
	 * loadKTX2; used to bake extracted asset textures to standalone files.
	 *
	 * @param srgb When true, the image's format is tagged with its sRGB Vulkan variant (same bits,
	 *        only the format field changes) so the GPU sampler decodes sRGB→linear on read. Use it for
	 *        color (base-color) textures; leave false for linear data (normal / ORM).
	 * @param compression How to encode the stored pixels. The BC targets write a plain, already-
	 *        compressed KTX2, so loadKTX2 uploads it with no transcode — that is the point of baking.
	 *        Only the 8-bit LDR formats can be compressed; anything else is stored verbatim.
	 * @throws std::runtime_error if the file cannot be written or the format has no KTX2 mapping.
	 */
	void
	writeKTX2(
		const ImageData&             image,
		const std::filesystem::path& path,
		bool                         srgb        = false,
		Ktx2Compression              compression = Ktx2Compression::kBasisUASTC);
}
