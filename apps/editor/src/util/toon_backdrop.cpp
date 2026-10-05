#include "util/toon_backdrop.h"
#include <algorithm>
#include <bgl/glm.h>
#include <bgl/types/BackdropGradient.h>
#include <cmath>
#include <qlogging.h>
#include <string_view>

namespace editor
{
	namespace
	{
		// The upper bound is sanity only: bgl takes any finite non-negative colour.
		constexpr float c_MaxChannel = 64.0f;

		glm::vec3
		ClampColor(glm::vec3 color, std::string_view who, std::string_view name)
		{
			for (int i = 0; i < 3; ++i)
			{
				const float value = color[i];
				const float clamped =
					std::isfinite(value) ? std::clamp(value, 0.0f, c_MaxChannel) : 0.0f;
				if (clamped != value)
				{
					qWarning(
						"%.*s: toonBackdrop %.*s.%c %.3f out of range, using %.3f",
						static_cast<int>(who.size()),
						who.data(),
						static_cast<int>(name.size()),
						name.data(),
						"rgb"[i],
						static_cast<double>(value),
						static_cast<double>(clamped));
				}
				color[i] = clamped;
			}
			return color;
		}
	}

	bgl::BackdropGradient
	ClampToonBackdrop(bgl::BackdropGradient gradient, std::string_view who)
	{
		gradient.bottom = ClampColor(gradient.bottom, who, "bottom");
		gradient.top    = ClampColor(gradient.top, who, "top");
		return gradient;
	}
}
