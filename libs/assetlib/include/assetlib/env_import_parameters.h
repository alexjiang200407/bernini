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

	/**
	 * The revision of the float sources an environment import writes: `equirectToCube`, `skyChain`,
	 * `prefilterRadiance` and `irradianceSh` as they stand. A `.ktx2` has nowhere to carry a token,
	 * so the import document records this beside the source's stamp, as a mesh import records
	 * `c_TextureBakeToken`; one written under another revision is stale whatever the stamp says.
	 *
	 * Moves on any change to the pixels those four produce, to a fresh random value, never a
	 * counter. Unlike `c_TextureBakeToken` it has no `TokenCanary_test` pin: the four run through
	 * libm's trigonometry, whose last bits differ between platforms, so a pinned hash would fail on
	 * the other one for a reason that is not a change. Remembering the bump is the author's.
	 */
	inline constexpr uint64_t c_EnvSourceBakeToken = 0x0f5c965369abe169ull;
}
