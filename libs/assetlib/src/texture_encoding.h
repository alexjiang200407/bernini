#pragma once

#include <assetlib/image_io.h>

#include <cstdint>
#include <string_view>
namespace assetlib
{
	/** What a map is for, which is what decides how it is stored. */
	enum class TextureRole : uint8_t
	{
		kBaseColor,
		kBaseColorWithAlpha,
		kOrm,
		kNormal,
		kGeometryOcclusion,
		kSurfaceSlot,
		kEnvironmentLdr,
		kEnvironmentHdr,
		// A Basis-supercompressed file, transcoded when it is loaded for the GPU.
		kTranscodeAtLoad,
		kCount,
	};

	/**
	 * How one role is stored. `tag` is the encoding's name wherever one is written down -- a key, a
	 * file name -- and is stable across any reordering of Ktx2Compression.
	 *
	 * A `kNone` row stores a float image as `E5B9G9R9_UFLOAT_PACK32` (packRgb9e5); every other row
	 * stores 8-bit texels in that block format.
	 */
	struct TextureEncoding
	{
		std::string_view tag;
		Ktx2Compression  compression;
	};

	/**
	 * The one table every bake and the load transcode read. A change to any row, or to what the
	 * encoder makes of one, is a bump of c_TextureEncodingToken; TokenCanary_test pins both.
	 */
	[[nodiscard]] TextureEncoding
	textureEncoding(TextureRole role) noexcept;
}
