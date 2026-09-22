#include "texture_encoding.h"

#include <assetlib/image_io.h>

#include <cassert>

namespace assetlib
{
	TextureEncoding
	textureEncoding(TextureRole role) noexcept
	{
		switch (role)
		{
		case TextureRole::kBaseColor:
			return { "bc1", Ktx2Compression::kBC1_RGB };
		// BC1 has no alpha libktx can write, so a base colour that carries one takes BC7.
		case TextureRole::kBaseColorWithAlpha:
			return { "bc7", Ktx2Compression::kBC7_RGBA };
		case TextureRole::kOrm:
			return { "bc7", Ktx2Compression::kBC7_RGBA };
		case TextureRole::kNormal:
			return { "bc5", Ktx2Compression::kBC5_RG };
		case TextureRole::kGeometryOcclusion:
			return { "bc4", Ktx2Compression::kBC4_R };
		case TextureRole::kSurfaceSlot:
			return { "bc7", Ktx2Compression::kBC7_RGBA };
		case TextureRole::kEnvironmentLdr:
			return { "bc7srgb", Ktx2Compression::kBC7_RGBA };
		case TextureRole::kEnvironmentHdr:
			return { "rgb9e5", Ktx2Compression::kNone };
		case TextureRole::kTranscodeAtLoad:
			return { "bc7", Ktx2Compression::kBC7_RGBA };
		case TextureRole::kCount:
			break;
		}
		assert(false && "assetlib::textureEncoding: not a role");
		return { "none", Ktx2Compression::kNone };
	}
}
