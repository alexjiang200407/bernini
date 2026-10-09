#pragma once
#include <bgl/types/DirectionalLightDesc.h>
#include <cmath>
#include <numbers>

namespace editor
{
	/**
	 * The sun an editor view is lit by: for toon content, from 30 degrees above, in front of a
	 * character facing +Z -- where the viewports' cameras start -- in a warm white, since a cel band
	 * needs a sun to fall from; for anything else none, so PBR is lit by its environment alone as
	 * Blender's viewport lights it, and the environment's own sun is not counted twice.
	 */
	[[nodiscard]] inline bgl::DirectionalLightDesc
	EditorSun(const bool toon) noexcept
	{
		constexpr float c_Elevation = std::numbers::pi_v<float> / 6.0f;
		return bgl::DirectionalLightDesc{
			.direction = glm::vec3(0.0f, -std::sin(c_Elevation), -std::cos(c_Elevation)),
			.color     = glm::vec3(1.0f, 0.98f, 0.94f),
			.intensity = toon ? 1.0f : 0.0f,
		};
	}
}
