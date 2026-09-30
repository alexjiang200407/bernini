#pragma once
#include <bgpu/types/Viewport.h>
#include <core/glm.h>

namespace bgl
{
	/**
	 * What one world unit spans on the render grid, in pixels, at a distance of one: half the
	 * viewport's height times the projection's y scale -- the length of the view-projection's y row,
	 * the view being a rotation. Take the unjittered matrix; the viewport is the render grid's.
	 *
	 * The editor's LOD view asks the same question through game::PixelsPerUnit, and LodSelect_test
	 * holds the two to one answer.
	 */
	[[nodiscard]] inline float
	PixelsPerUnit(const bgpu::Viewport& viewport, const glm::mat4& unjitteredViewProj) noexcept
	{
		const float yScale = glm::length(
			glm::vec3(
				unjitteredViewProj[0][1],
				unjitteredViewProj[1][1],
				unjitteredViewProj[2][1]));
		return 0.5f * (viewport.maxY - viewport.minY) * yScale;
	}
}
