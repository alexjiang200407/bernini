#pragma once
#include <core/glm.h>

namespace game
{
	/**
	 * A parametric ray: a point at `t` is `origin + direction * t`. The direction need not be
	 * normalized -- every intersection below returns `t` in units of `direction`, so a ray and its
	 * `Transformed` image parameterize the same points and their `t` values compare directly.
	 */
	struct Ray
	{
		glm::vec3 origin;
		glm::vec3 direction;
	};

	/**
	 * Where `ray` pierces the triangle `a b c`, tested from both sides.
	 *
	 * @return The parametric distance, or nullopt for a miss, a triangle behind the origin, or one
	 *         parallel to the ray.
	 */
	[[nodiscard]] std::optional<float>
	IntersectTriangle(
		const Ray&       ray,
		const glm::vec3& a,
		const glm::vec3& b,
		const glm::vec3& c) noexcept;

	/**
	 * Where `ray` enters the box, or 0 for an origin already inside.
	 *
	 * @return The parametric distance to the entry face, or nullopt when the ray misses or the box
	 *         is entirely behind the origin.
	 */
	[[nodiscard]] std::optional<float>
	IntersectAabb(const Ray& ray, const glm::vec3& boxMin, const glm::vec3& boxMax) noexcept;

	/**
	 * Where `ray` first meets the sphere's surface: the near side, or the far side for an origin
	 * inside.
	 *
	 * @return The parametric distance, or nullopt when the ray misses or the sphere is entirely
	 *         behind the origin.
	 */
	[[nodiscard]] std::optional<float>
	IntersectSphere(const Ray& ray, const glm::vec3& center, float radius) noexcept;

	/**
	 * `ray` carried into another space. The direction is deliberately not renormalized: a `t` found
	 * in the transformed space parameterizes the original ray too, so hits from differently
	 * transformed spaces compare without conversion.
	 */
	[[nodiscard]] Ray
	Transformed(const Ray& ray, const glm::mat4& transform) noexcept;

	/**
	 * The world-space ray from the eye through `pixel` of a viewport rendered with
	 * `viewProjection`. The origin sits on the near plane and `t = 1` lands on the far plane.
	 *
	 * `pixel` and `viewportSize` must be in the same units (any uniform scale of both gives the
	 * same ray, so logical and physical pixels both work). Pixel y grows downward, as in every
	 * windowing system.
	 */
	[[nodiscard]] Ray
	RayThroughPixel(
		const glm::mat4& viewProjection,
		const glm::vec2& pixel,
		const glm::vec2& viewportSize) noexcept;
}
