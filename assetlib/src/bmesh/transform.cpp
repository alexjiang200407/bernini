#include <assetlib/bmesh/Node.h>

namespace assetlib::bmesh
{
	glm::mat4
	toMatrix(const Transform& transform) noexcept
	{
		auto matrix = glm::translate(glm::mat4(1.0f), transform.translation);
		matrix *= glm::mat4_cast(transform.rotation);
		matrix = glm::scale(matrix, transform.scale);
		return matrix;
	}
}
