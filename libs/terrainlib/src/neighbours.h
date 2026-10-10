#pragma once
#include <array>
#include <core/glm.h>

namespace terrain
{
	/** The eight samples around one, as offsets along x and z. */
	inline const std::array<glm::ivec2, 8> c_Neighbours{ {
		{ -1, -1 },
		{ 0, -1 },
		{ 1, -1 },
		{ -1, 0 },
		{ 1, 0 },
		{ -1, 1 },
		{ 0, 1 },
		{ 1, 1 },
	} };
}
