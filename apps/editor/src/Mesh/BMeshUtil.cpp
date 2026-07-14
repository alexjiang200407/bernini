#include "Mesh/BMeshUtil.h"

namespace bmesh
{
	glm::mat4
	WorldTransform(const assetlib::BMesh& mesh, uint32_t nodeIndex)
	{
		auto     world = glm::mat4(1.0f);
		uint32_t index = nodeIndex;
		while (index != assetlib::c_InvalidIndex && index < mesh.nodes.size())
		{
			const assetlib::Node& node = mesh.nodes[index];
			world                      = assetlib::toMatrix(node.localTransform) * world;
			index                      = node.parent;
		}
		return world;
	}

	std::string
	NameFromPool(const std::vector<char>& pool, uint32_t offset)
	{
		if (offset == 0 || offset >= pool.size())
			return {};
		return std::string(pool.data() + offset);
	}

	void
	GrowBounds(
		const glm::mat4& transform,
		const glm::vec3& boxMin,
		const glm::vec3& boxMax,
		glm::vec3&       outMin,
		glm::vec3&       outMax)
	{
		for (int corner = 0; corner < 8; ++corner)
		{
			const auto point = glm::vec3(
				(corner & 1) ? boxMax.x : boxMin.x,
				(corner & 2) ? boxMax.y : boxMin.y,
				(corner & 4) ? boxMax.z : boxMin.z);

			const auto world = glm::vec3(transform * glm::vec4(point, 1.0f));
			outMin           = glm::min(outMin, world);
			outMax           = glm::max(outMax, world);
		}
	}
}
