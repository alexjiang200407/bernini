#pragma once
#include <bgl/types/BloomSettings.h>
#include <bgl/types/ColorSplitSettings.h>
#include <bgl/types/FilmGrainSettings.h>
#include <bgl/types/ToonGradeSettings.h>
#include <optional>

namespace bgl
{
	/**
	 * The toon post-process: Blender's Standard view, the exposed value clamped to [0, 1] and no
	 * curve, so a toon look reaches the display as authored. An absent effect is off. See
	 * docs/passes.md § Scene colour.
	 */
	struct ToonPostProcess
	{
		// Screened over the clamped scene, so a glow stops short of white rather than clipping.
		std::optional<BloomSettings> bloom;

		std::optional<ToonGradeSettings>  grade;
		std::optional<FilmGrainSettings>  grain;
		std::optional<ColorSplitSettings> split;
	};
}
