#pragma once
#include <bgl/types/BloomSettings.h>
#include <bgl/types/ColorGradeSettings.h>
#include <bgl/types/ColorSplitSettings.h>
#include <bgl/types/DisplayCurve.h>
#include <bgl/types/FilmGrainSettings.h>
#include <optional>

namespace bgl
{
	/**
	 * Everything between a target's resolved scene and its display, as one value, set whole with
	 * IRenderTarget::SetPostProcess. An absent effect is off. See docs/passes.md § Scene colour.
	 */
	struct PostProcess
	{
		DisplayCurve curve = DisplayCurve::kAgX;

		// Added to the scene in linear radiance, ahead of the curve, which rolls it off.
		std::optional<BloomSettings> bloom;

		std::optional<ColorGradeSettings> grade;
		std::optional<FilmGrainSettings>  grain;
		std::optional<ColorSplitSettings> split;
	};
}
