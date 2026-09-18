#include "postprocess/color_grade.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <cmath>
#include <core/glm.h>
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

		// Unity's parameterisation of the CIE daylight locus: x from temperature, y on the locus
		// through it, then tint as a step off it along y.
		float
		DaylightLocusY(float x) noexcept
		{
			return 2.87f * x - 3.0f * x * x - 0.27509507f;
		}

		glm::vec3
		XyToCat02Lms(float x, float y) noexcept
		{
			const float bigX = x / y;
			const float bigZ = (1.0f - x - y) / y;

			return glm::vec3(
				0.7328f * bigX + 0.4296f - 0.1624f * bigZ,
				-0.7036f * bigX + 1.6975f + 0.0061f * bigZ,
				0.0030f * bigX + 0.0136f + 0.9834f * bigZ);
		}

		constexpr float c_D65X = 0.31271f;
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

	glm::vec3
	WhiteBalanceLmsScale(float temperature, float tint) noexcept
	{
		// Cooler whites sit further along the locus per unit than warmer ones, as in Unity.
		const float t1 = temperature / 65.0f;
		const float t2 = tint / 65.0f;

		const float x = c_D65X - t1 * (t1 < 0.0f ? 0.1f : 0.05f);
		const float y = DaylightLocusY(x) + t2 * 0.05f;

		// D65 through the same locus rather than its tabulated LMS, so zero is exactly one.
		return XyToCat02Lms(c_D65X, DaylightLocusY(c_D65X)) / XyToCat02Lms(x, y);
	}
}
