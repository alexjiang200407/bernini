#pragma once
#include <cstdint>

namespace assetlib
{
	/**
	 * What an environment import computes its float sources with -- the half of an import that
	 * changes the pixels, and so the half an import document records and keys on.
	 *
	 * Its own header, apart from envmap.h, because an import document holds one by value and every
	 * mesh-only reader of that document would otherwise compile the whole convolution API.
	 *
	 * Not `skyMipLevel` or `threads`: the first is presentation, authored on the `.benv`, and the
	 * second changes how fast the same pixels arrive.
	 */
	struct EnvironmentImportParameters
	{
		uint32_t skyFaceSize = 512;

		// Levels in the sky's defocus chain -- see skyChain. Clamped at import to what
		// `skyFaceSize` can carry; this is the request.
		uint32_t skyMips = 6;

		uint32_t prefilterFaceSize  = 256;
		uint32_t prefilterMips      = 7;  // must match the shader's MAX_REFLECTION_LOD + 1
		uint32_t prefilterSamples   = 128;
		uint32_t irradianceFaceSize = 128;

		bool
		operator==(const EnvironmentImportParameters&) const = default;
	};
}
