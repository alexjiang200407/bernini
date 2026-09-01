#pragma once
#include "idl/CullView.h"
#include <bgl/glm.h>

namespace bgl
{
	/**
	 * Six normalized clip planes — left, right, bottom, top, near, far — as (normal, distance)
	 * with the positive half-space inside.
	 */
	struct FrustumPlanes
	{
		glm::vec4 planes[6];

		[[nodiscard]] const glm::vec4&
		operator[](size_t i) const noexcept
		{
			return planes[i];
		}
	};

	/** @pre [0, 1] clip depth (the project's glm configuration). */
	[[nodiscard]] FrustumPlanes
	ExtractFrustumPlanes(const glm::mat4& viewProj) noexcept;

	// Conservative: near a corner it can report intersecting for a sphere that is outside.
	[[nodiscard]] bool
	SphereIntersectsFrustum(
		const FrustumPlanes& frustum,
		const glm::vec3&     center,
		float                radius) noexcept;

	[[nodiscard]] idl::CullView
	BuildCullView(const glm::mat4& viewProj) noexcept;
}
