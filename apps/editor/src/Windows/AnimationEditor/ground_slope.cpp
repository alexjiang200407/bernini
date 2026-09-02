#include "ground_slope.h"

namespace editor
{
	namespace
	{
		// A rotation about Z: what tilts a floor so it rises toward +X.
		glm::mat4
		Tilt(const float degrees)
		{
			return glm::rotate(glm::mat4(1.0f), glm::radians(degrees), glm::vec3(0.0f, 0.0f, 1.0f));
		}
	}

	bgl::GroundPlaneDesc
	GroundForSlope(const float degrees)
	{
		return { glm::vec3(0.0f), glm::vec3(Tilt(degrees) * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)) };
	}

	glm::mat4
	FloorTransformForSlope(const float degrees)
	{
		// The plane's own normal is +Z, so it is laid flat about X first.
		const glm::mat4 flat =
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		return Tilt(degrees) * flat;
	}
}
