#pragma once
#include <bgpu/types/Format.h>
#include <cmath>
#include <core/glm.h>
#include <cstdint>
#include <string_view>

namespace bgl
{
	/** Texels along each side of a view's ground-colour texture. */
	constexpr uint32_t c_GroundColorTexels = 1024;

	constexpr bgpu::Format     c_GroundColorFormat = bgpu::Format::SRGBA8_UNORM;
	constexpr std::string_view c_GroundColorName   = "groundColor";

	/**
	 * Where a view's ground-colour texture lies: the world xz of the outer corner of its texel
	 * (0, 0), and its side in metres. A side of zero means the view draws no terrain grass that takes
	 * its ground's colour, and has no texture.
	 */
	struct GroundColorRect
	{
		glm::vec2 origin = glm::vec2(0.0f);
		float     size   = 0.0f;
	};

	/**
	 * The square of `texels` a side around `camera` that reaches at least `reach` from it every way,
	 * its corner snapped to whole texels, so the ground a texel covers does not move as the camera
	 * does and a still ground never shimmers. One texel each side is slack the snap takes up.
	 */
	[[nodiscard]] inline GroundColorRect
	GroundColorRectAround(
		const glm::vec3& camera,
		const float      reach,
		const uint32_t   texels) noexcept
	{
		if (!(reach > 0.0f) || !std::isfinite(reach) || texels <= 2)
		{
			return {};
		}
		const float     texel = 2.0f * reach / static_cast<float>(texels - 2);
		const glm::vec2 corner =
			glm::floor(glm::vec2(camera.x, camera.z) / texel) - static_cast<float>(texels / 2);
		return { .origin = corner * texel, .size = texel * static_cast<float>(texels) };
	}
}
