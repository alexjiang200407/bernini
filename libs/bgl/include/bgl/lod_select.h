#pragma once
#include <bgl/LodLevel.h>
#include <bgl/Viewport.h>
#include <bgl/glm.h>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

// The size test the cull chooses a placement's level of detail by (lib/culling/lod_select.slang),
// for a tool that says which level a placement draws without reading the GPU's choice back. The
// renderer computes its own inputs through these too, so the two cannot drift.
namespace bgl
{
	/**
	 * What one world unit spans on the render grid, in pixels, at a distance of one: half the
	 * viewport's height times the projection's y scale, which is the length of the view-projection's
	 * y row since the view is a rotation. Take the unjittered matrix -- the jitter is a sub-pixel
	 * translation and has no size. The viewport is the render grid's, after the render scale.
	 */
	[[nodiscard]] inline float
	PixelsPerUnit(const Viewport& viewport, const glm::mat4& unjitteredViewProj) noexcept
	{
		const float yScale = glm::length(
			glm::vec3(
				unjitteredViewProj[0][1],
				unjitteredViewProj[1][1],
				unjitteredViewProj[2][1]));
		return 0.5f * (viewport.maxY - viewport.minY) * yScale;
	}

	/** (center, radius) circumscribing the box, so it is conservative for whatever the box held. */
	[[nodiscard]] inline glm::vec4
	BoundingSphereOf(const glm::vec3& minBound, const glm::vec3& maxBound) noexcept
	{
		const glm::vec3 center = (minBound + maxBound) * 0.5f;
		return glm::vec4(center, glm::distance(maxBound, center));
	}

	/** `sphere` placed by `world`, its radius grown by the largest axis scale. */
	[[nodiscard]] inline glm::vec4
	TransformSphere(const glm::mat4& world, const glm::vec4& sphere) noexcept
	{
		const float maxScaleSq = glm::max(
			glm::dot(glm::vec3(world[0]), glm::vec3(world[0])),
			glm::max(
				glm::dot(glm::vec3(world[1]), glm::vec3(world[1])),
				glm::dot(glm::vec3(world[2]), glm::vec3(world[2]))));
		return glm::vec4(
			glm::vec3(world * glm::vec4(glm::vec3(sphere), 1.0f)),
			sphere.w * glm::sqrt(maxScaleSq));
	}

	/**
	 * A placed sphere's diameter on screen, in pixels, at its true distance from the camera -- not
	 * view-space depth, which would change a level as the camera turns. A camera inside the sphere
	 * sees it larger than any threshold.
	 */
	[[nodiscard]] inline float
	ProjectedDiameter(
		const glm::vec4& worldSphere,
		const glm::vec3& cameraPos,
		float            pixelsPerUnit) noexcept
	{
		const float distance = glm::distance(glm::vec3(worldSphere), cameraPos);
		if (distance <= worldSphere.w)
			return std::numeric_limits<float>::max();
		return 2.0f * worldSphere.w * pixelsPerUnit / distance;
	}

	/**
	 * The finest level whose floor, scaled by `pixelScale`, `size` meets, or `minPixels.size()` --
	 * the draw-nothing tier -- when it meets none. `minPixels` is a mesh's per-level thresholds.
	 */
	[[nodiscard]] inline uint32_t
	LevelBySize(std::span<const float> minPixels, float size, float pixelScale) noexcept
	{
		for (uint32_t level = 0; level < minPixels.size(); ++level)
		{
			if (size >= minPixels[level] * pixelScale)
				return level;
		}
		return static_cast<uint32_t>(minPixels.size());
	}

	/**
	 * The level the cull draws this frame when it drew `previous` the last: coarser happens at the
	 * threshold, finer only once `size` clears that level's floor by cLodHysteresis. nullopt for a
	 * placement with no last choice.
	 */
	[[nodiscard]] inline uint32_t
	ChooseLevel(
		std::span<const float>  minPixels,
		float                   size,
		float                   pixelScale,
		std::optional<uint32_t> previous) noexcept
	{
		const uint32_t atThreshold = LevelBySize(minPixels, size, pixelScale);
		if (!previous.has_value() || atThreshold >= *previous)
			return atThreshold;
		return glm::min(
			LevelBySize(minPixels, size, pixelScale * (1.0f + cLodHysteresis)),
			*previous);
	}
}
