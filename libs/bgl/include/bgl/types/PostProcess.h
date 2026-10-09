#pragma once
#include <bgl/types/BloomSettings.h>
#include <bgl/types/ColorGradeSettings.h>
#include <bgl/types/ColorSplitSettings.h>
#include <bgl/types/FilmGrainSettings.h>
#include <optional>

namespace bgl
{
	/**
	 * Everything between a target's resolved scene and its display, as one value, set whole with
	 * IRenderTarget::SetPostProcess. The curve is always Blender's AgX, which rolls highlights off
	 * and desaturates toward white; an absent effect is off. See docs/passes.md § Scene colour.
	 */
	struct PostProcess
	{
		// Added to the scene in linear radiance, ahead of the curve, which rolls it off.
		std::optional<BloomSettings> bloom;

		std::optional<ColorGradeSettings> grade;
		std::optional<FilmGrainSettings>  grain;
		std::optional<ColorSplitSettings> split;
	};
}
