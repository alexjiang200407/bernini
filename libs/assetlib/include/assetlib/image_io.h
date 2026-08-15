#pragma once
#include <core/file/IFileSystem.h>

namespace assetlib
{
	struct ImageData;

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
	 * @param maxDim When non-zero, only the tail of the stored mip chain is returned: the top level
	 *        is the smallest stored mip whose longer edge still covers `maxDim` (the whole image when
	 *        it is already smaller), and for a block-compressed format it backs off further to the
	 *        first block-aligned level, since D3D12 rejects an unaligned top level. Selects among
	 *        stored mips, never resamples -- an image with no smaller mips comes back whole. For a
	 *        consumer that displays at a known small size, this cuts the decoded bytes and the
	 *        upload that follows by the skipped levels.
	 * @throws std::runtime_error if the file cannot be read, decoded, or carries an unmapped format.
	 */
	[[nodiscard]] ImageData
	loadKTX2(
		const std::filesystem::path& path,
		Ktx2Decode                   decode = Ktx2Decode::kGpu,
		uint32_t                     maxDim = 0);

	/**
	 * The mounted overload: `path` is data-root-relative and resolved through `fileSystem`, so the
	 * texture may equally be a loose file or an entry in an archive.
	 *
	 * Reads the whole container, unlike the chunked loaders: a `.ktx2` is decoded end to end, and
	 * `maxDim` selects among mips that libktx has already loaded rather than deciding what to fetch.
	 *
	 * @throws std::runtime_error if the texture is absent, cannot be read, or cannot be decoded.
	 */
	[[nodiscard]] ImageData
	loadKTX2(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		Ktx2Decode                     decode = Ktx2Decode::kGpu,
		uint32_t                       maxDim = 0);

	/**
	 * loadKTX2 over a `.ktx2` already in memory, for a container that embeds one.
	 *
	 * @throws std::runtime_error if the bytes are not a decodable KTX2.
	 */
	[[nodiscard]] ImageData
	decodeKTX2(std::span<const std::byte> bytes, Ktx2Decode decode = Ktx2Decode::kGpu);

	/**
	 * Repacks a float image as `E5B9G9R9_UFLOAT_PACK32`: a 5-bit exponent shared across a 9-bit
	 * mantissa per channel, 4 bytes a texel instead of 16.
	 *
	 * This is the format HDR maps ship in. It is filterable on every backend without an optional
	 * feature -- WebGPU core `rgb9e5ufloat`, D3D12 `R9G9B9E5_SHAREDEXP`, Metal `RGB9E5Float` -- which
	 * is why it is preferred over BC6H, four times smaller again but unreachable on Apple GPUs, and
	 * over `R11G11B10`, the same size but carrying only 5 mantissa bits on blue, which bands in a sky
	 * gradient.
	 *
	 * Alpha is dropped: the format has no alpha, and radiance has no use for one.
	 *
	 * @param image A `R32G32B32A32_SFLOAT` image; geometry, mips and faces are preserved.
	 * @throws std::runtime_error if `image` is not that format.
	 */
	[[nodiscard]] ImageData
	packRgb9e5(const ImageData& image);

	/**
	 * Unpacks an `E5B9G9R9_UFLOAT_PACK32` image back to `R32G32B32A32_SFLOAT`, alpha 1.
	 *
	 * Exact: the shared exponent and the three mantissas are a subset of what a float can hold, so
	 * this recovers the values `packRgb9e5` stored -- not the ones it was given, which it quantized.
	 *
	 * Exists because the CPU bake path reads float and a shipped map is RGB9E5, which is the only
	 * form left when a route's float source is gone. Re-convolving a baked map costs a generation of
	 * quantization, so prefer the source where there is one.
	 *
	 * @param image An `E5B9G9R9_UFLOAT_PACK32` image; geometry, mips and faces are preserved.
	 * @throws std::runtime_error if `image` is not that format.
	 */
	[[nodiscard]] ImageData
	unpackRgb9e5(const ImageData& image);

	/**
	 * writeKTX2 into a buffer instead of a file, for embedding in a container.
	 *
	 * @throws std::runtime_error if the image cannot be encoded.
	 */
	[[nodiscard]] std::vector<std::byte>
	encodeKTX2(
		const ImageData& image,
		bool             srgb        = false,
		Ktx2Compression  compression = Ktx2Compression::kNone);

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

	/** The mounted overload of loadKTX2Preview; see loadKTX2's. */
	[[nodiscard]] ImageData
	loadKTX2Preview(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		uint32_t                       maxDim = 128);

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
