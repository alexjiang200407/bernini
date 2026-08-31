#pragma once
#include <core/glm.h>

/**
 * The posed tangent a VAT texel carries: not a vector, but how far the bind tangent, carried onto
 * the posed normal by the shortest arc, must still turn about that normal. The bake writes this
 * twist into the normal texture's alpha; programs/forward/VatMesh.slang undoes it with the same two
 * rotations, so the formulas here and there must agree to the step.
 */
namespace assetlib
{
	/**
	 * `v` carried by the shortest arc that takes unit `from` onto unit `to`. Antiparallel
	 * directions have no shortest arc; `v` is then returned as it is, and the twist absorbs the
	 * rest -- the shader makes the same choice.
	 */
	[[nodiscard]] inline glm::vec3
	rotateByShortestArc(const glm::vec3& v, const glm::vec3& from, const glm::vec3& to) noexcept
	{
		const float     c    = glm::dot(from, to);
		const glm::vec3 axis = glm::cross(from, to);
		if (c < -1.0f + 1e-4f)
			return v;
		return v * c + glm::cross(axis, v) + axis * (glm::dot(axis, v) / (1.0f + c));
	}

	/**
	 * Radians in (-pi, pi] that `bindTangent`, carried onto `posedNormal`, turns about it to land
	 * on `posedTangent`. All four unit. Zero when either tangent is absent (zero).
	 */
	[[nodiscard]] inline float
	vatTangentTwist(
		const glm::vec3& bindNormal,
		const glm::vec3& bindTangent,
		const glm::vec3& posedNormal,
		const glm::vec3& posedTangent) noexcept
	{
		const glm::vec3 carried   = rotateByShortestArc(bindTangent, bindNormal, posedNormal);
		const glm::vec3 bitangent = glm::cross(posedNormal, carried);
		const float     sine      = glm::dot(posedTangent, bitangent);
		const float     cosine    = glm::dot(posedTangent, carried);
		if (sine == 0.0f && cosine == 0.0f)
			return 0.0f;
		return std::atan2(sine, cosine);
	}

	/** The shader's reconstruction: the bind tangent carried onto `posedNormal`, then twisted. */
	[[nodiscard]] inline glm::vec3
	vatPosedTangent(
		const glm::vec3& bindNormal,
		const glm::vec3& bindTangent,
		const glm::vec3& posedNormal,
		float            twist) noexcept
	{
		const glm::vec3 carried = rotateByShortestArc(bindTangent, bindNormal, posedNormal);
		return carried * std::cos(twist) + glm::cross(posedNormal, carried) * std::sin(twist);
	}

	/** The alpha channel's encoding of a twist: a half turn either way spans the unorm. */
	[[nodiscard]] inline float
	packTwist(float twist) noexcept
	{
		return twist / glm::two_pi<float>() + 0.5f;
	}

	[[nodiscard]] inline float
	unpackTwist(float unorm) noexcept
	{
		return (unorm - 0.5f) * glm::two_pi<float>();
	}
}
