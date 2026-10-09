#pragma once

#include <cstdint>
namespace bgl
{
	/**
	 * The curve a target's frame ends in, after the grade. See docs/passes.md § Scene colour.
	 */
	enum class DisplayCurve : uint32_t
	{
		// Blender's AgX: rolls highlights off and desaturates bright colour toward white. The look of
		// a realistic, physically lit scene, and the one Blender's renders are compared through.
		kAgX = 0,

		// Gran Turismo's (Uchimura 2017), per channel: the identity through the midtones, so a
		// painted colour is shown as painted, and a shoulder for highlights. For stylized content.
		kGranTurismo,

		kCount,
	};
}
