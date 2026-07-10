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
