#include "ground_slope.h"

namespace editor
{
	namespace
	{
		// A rotation about Z tilting a floor so it rises toward +X, then turned about Y so it rises
		// toward the heading instead. The tilt is applied first: a heading is where uphill points,
		// and turning after tilting is what keeps that true at every slope.
		glm::mat4
		Tilt(const float degrees, const float headingDegrees)
		{
			const glm::mat4 rise =
				glm::rotate(glm::mat4(1.0f), glm::radians(degrees), glm::vec3(0.0f, 0.0f, 1.0f));
			const glm::mat4 turn = glm::rotate(
				glm::mat4(1.0f),
				glm::radians(-headingDegrees),
				glm::vec3(0.0f, 1.0f, 0.0f));
			return turn * rise;
		}
	}

	bgl::GroundPlaneDesc
	GroundForSlope(const float degrees, const float headingDegrees)
	{
		return { glm::vec3(0.0f),
			     glm::vec3(Tilt(degrees, headingDegrees) * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)) };
	}

	glm::mat4
	FloorTransformForSlope(const float degrees, const float headingDegrees)
	{
		// The plane's own normal is +Z, so it is laid flat about X first.
		const glm::mat4 flat =
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		return Tilt(degrees, headingDegrees) * flat;
	}
}
