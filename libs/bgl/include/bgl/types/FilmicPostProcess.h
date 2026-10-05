#pragma once
#include <bgl/types/BloomSettings.h>
#include <bgl/types/ColorGradeSettings.h>
#include <bgl/types/ColorSplitSettings.h>
#include <bgl/types/FilmGrainSettings.h>
#include <optional>

namespace bgl
{
	/**
	 * The filmic post-process: Blender's AgX, a curve that rolls highlights off and desaturates
	 * toward white. An absent effect is off. See docs/passes.md § Scene colour.
	 */
	struct FilmicPostProcess
	{
		// Added to the scene in linear radiance, ahead of the curve, which rolls it off.
		std::optional<BloomSettings> bloom;

		std::optional<ColorGradeSettings> grade;
		std::optional<FilmGrainSettings>  grain;
		std::optional<ColorSplitSettings> split;
	};
}
