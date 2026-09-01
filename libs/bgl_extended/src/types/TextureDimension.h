#pragma once

namespace bgl
{
	enum class TextureDimension : uint8_t
	{
		kUnknown,
		kTexture1D,
		kTexture1DArray,
		kTexture2D,
		kTexture2DArray,
		kTextureCube,
		kTextureCubeArray,
		kTexture2DMS,
		kTexture2DMSArray,
		kTexture3D
	};
}
