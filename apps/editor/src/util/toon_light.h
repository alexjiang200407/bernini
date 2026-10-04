#pragma once
#include <bgl/glm.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <cmath>
#include <numbers>

namespace editor
{
	/**
	 * The toon sun every editor view is lit by: from 30 degrees above, in front of a character
	 * facing +Z -- where the viewports' cameras start -- in a warm white. The editor sets no PBR sun,
	 * lighting PBR by its environment as Blender's viewport does, and the toon character model reads
	 * no environment, so without this every toon character in the editor would draw black.
	 */
	[[nodiscard]] inline bgl::DirectionalLightDesc
	DefaultToonLight() noexcept
	{
		constexpr float c_Elevation = std::numbers::pi_v<float> / 6.0f;
		return bgl::DirectionalLightDesc{
			.direction = glm::vec3(0.0f, -std::sin(c_Elevation), -std::cos(c_Elevation)),
			.color     = glm::vec3(1.0f, 0.98f, 0.94f),
			.intensity = 1.0f,
		};
	}
}
