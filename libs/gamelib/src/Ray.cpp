#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <gamelib/Ray.h>
#include <limits>
#include <optional>
#include <utility>

namespace game
{
	std::optional<float>
	IntersectTriangle(
		const Ray&       ray,
		const glm::vec3& a,
		const glm::vec3& b,
		const glm::vec3& c) noexcept
	{
		constexpr float c_DegenerateDeterminant = 1e-8f;

		const glm::vec3 edge1       = b - a;
		const glm::vec3 edge2       = c - a;
		const glm::vec3 crossDir    = glm::cross(ray.direction, edge2);
		const float     determinant = glm::dot(edge1, crossDir);

		if (std::abs(determinant) < c_DegenerateDeterminant)
			return std::nullopt;

		const float     inverse  = 1.0f / determinant;
		const glm::vec3 toOrigin = ray.origin - a;

		const float u = glm::dot(toOrigin, crossDir) * inverse;
		if (u < 0.0f || u > 1.0f)
			return std::nullopt;

		const glm::vec3 crossOrigin = glm::cross(toOrigin, edge1);

		const float v = glm::dot(ray.direction, crossOrigin) * inverse;
		if (v < 0.0f || u + v > 1.0f)
			return std::nullopt;

		const float t = glm::dot(edge2, crossOrigin) * inverse;
		if (t < 0.0f)
			return std::nullopt;

		return t;
	}

	std::optional<float>
	IntersectAabb(const Ray& ray, const glm::vec3& boxMin, const glm::vec3& boxMax) noexcept
	{
		constexpr float c_ParallelDirection = 1e-12f;

		float tEntry = 0.0f;
		float tExit  = std::numeric_limits<float>::max();

		for (int axis = 0; axis < 3; ++axis)
		{
			if (std::abs(ray.direction[axis]) < c_ParallelDirection)
			{
				if (ray.origin[axis] < boxMin[axis] || ray.origin[axis] > boxMax[axis])
					return std::nullopt;
				continue;
			}

			const float inverse = 1.0f / ray.direction[axis];
			float       tNear   = (boxMin[axis] - ray.origin[axis]) * inverse;
			float       tFar    = (boxMax[axis] - ray.origin[axis]) * inverse;
			if (tNear > tFar)
				std::swap(tNear, tFar);

			tEntry = std::max(tEntry, tNear);
			tExit  = std::min(tExit, tFar);
			if (tEntry > tExit)
				return std::nullopt;
		}

		return tEntry;
	}

	std::optional<float>
	IntersectSphere(const Ray& ray, const glm::vec3& center, float radius) noexcept
	{
		const glm::vec3 toOrigin = ray.origin - center;

		const float a = glm::dot(ray.direction, ray.direction);
		if (a == 0.0f)
			return std::nullopt;

		const float b            = 2.0f * glm::dot(toOrigin, ray.direction);
		const float c            = glm::dot(toOrigin, toOrigin) - radius * radius;
		const float discriminant = b * b - 4.0f * a * c;
		if (discriminant < 0.0f)
			return std::nullopt;

		const float root = std::sqrt(discriminant);

		float t = (-b - root) / (2.0f * a);
		if (t < 0.0f)
			t = (-b + root) / (2.0f * a);
		if (t < 0.0f)
			return std::nullopt;

		return t;
	}

	Ray
	Transformed(const Ray& ray, const glm::mat4& transform) noexcept
	{
		return { glm::vec3(transform * glm::vec4(ray.origin, 1.0f)),
			     glm::vec3(transform * glm::vec4(ray.direction, 0.0f)) };
	}

	Ray
	RayThroughPixel(
		const glm::mat4& viewProjection,
		const glm::vec2& pixel,
		const glm::vec2& viewportSize) noexcept
	{
		const glm::mat4 inverse = glm::inverse(viewProjection);

		const glm::vec2 uv = pixel / viewportSize;
		const glm::vec2 ndc(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

		// Clip z spans [0, 1] (GLM_FORCE_DEPTH_ZERO_TO_ONE), so these are the near and far planes.
		glm::vec4 nearPoint = inverse * glm::vec4(ndc, 0.0f, 1.0f);
		glm::vec4 farPoint  = inverse * glm::vec4(ndc, 1.0f, 1.0f);
		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;

		return { glm::vec3(nearPoint), glm::vec3(farPoint - nearPoint) };
	}
}
