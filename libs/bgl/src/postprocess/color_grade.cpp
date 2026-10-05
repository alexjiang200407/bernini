#include "postprocess/color_grade.h"
#include <core/glm.h>

namespace bgl
{
	namespace
	{
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
