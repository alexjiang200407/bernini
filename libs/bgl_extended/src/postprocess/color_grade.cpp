#include "postprocess/color_grade.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <cmath>
#include <format>
#include <string_view>

namespace bgl
{
	namespace
	{
		// Written as `!(lo <= v && v <= hi)` so a NaN fails it.
		void
		RequireWithin(std::string_view field, float value, float lo, float hi)
		{
			if (!(lo <= value && value <= hi))
			{
				throw GraphicsError(
					std::format("ColorGradeSettings::{} must be within [{}, {}]", field, lo, hi));
			}
		}

		void
		RequireNonNegative(std::string_view field, float value)
		{
			if (!(value >= 0.0f) || !std::isfinite(value))
			{
				throw GraphicsError(
					std::format("ColorGradeSettings::{} must be non-negative and finite", field));
			}
		}

		void
		RequirePositive(std::string_view field, float value)
		{
			if (!(value > 0.0f) || !std::isfinite(value))
			{
				throw GraphicsError(
					std::format("ColorGradeSettings::{} must be positive and finite", field));
			}
		}
	}

	void
	ValidateColorGradeSettings(const ColorGradeSettings& settings)
	{
		RequireWithin("temperature", settings.temperature, -100.0f, 100.0f);
		RequireWithin("tint", settings.tint, -100.0f, 100.0f);

		for (int c = 0; c < 3; ++c)
		{
			RequireNonNegative("slope", settings.slope[c]);
			RequireWithin("offset", settings.offset[c], -1.0f, 1.0f);
			RequirePositive("power", settings.power[c]);
		}

		RequireNonNegative("saturation", settings.saturation);
		RequireNonNegative("contrast", settings.contrast);
		RequireWithin("vignetteIntensity", settings.vignetteIntensity, 0.0f, 1.0f);

		if (!(settings.vignetteSmoothness > 0.0f && settings.vignetteSmoothness <= 1.0f))
		{
			throw GraphicsError("ColorGradeSettings::vignetteSmoothness must be within (0, 1]");
		}
	}
}
