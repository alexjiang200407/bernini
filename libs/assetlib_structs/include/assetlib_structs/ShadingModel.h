#pragma once

namespace assetlib
{
	/**
	 * How a surface, and the environment lighting it, is shaded.
	 *
	 * One enum for both sides of the pair: a `.bmaterial` names the model it shades under, and a
	 * `.benv` names the model its lighting is authored for, so the two cannot disagree about what
	 * `pbr` means.
	 */
	enum class ShadingModel : uint32_t
	{
		kPbr = 0,
		kCount,
	};
}
