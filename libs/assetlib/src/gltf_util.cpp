#include "gltf_util.h"

namespace assetlib
{
	Transform
	decomposeTransform(const glm::mat4& matrix) noexcept
	{
		Transform transform{};
		transform.translation = glm::vec3(matrix[3]);

		const glm::vec3 scale(
			glm::length(glm::vec3(matrix[0])),
			glm::length(glm::vec3(matrix[1])),
			glm::length(glm::vec3(matrix[2])));
		transform.scale = scale;

		// A zero axis would divide by zero; a collapsed basis has no rotation to recover anyway.
		const auto safe = [](float value) { return value == 0.0f ? 1.0f : value; };

		const glm::mat3 rotation(
			glm::vec3(matrix[0]) / safe(scale.x),
			glm::vec3(matrix[1]) / safe(scale.y),
			glm::vec3(matrix[2]) / safe(scale.z));
		transform.rotation = glm::quat_cast(rotation);
		return transform;
	}

	Transform
	readNodeTransform(const tinygltf::Node& node) noexcept
	{
		Transform transform{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };

		if (node.matrix.size() == 16)
		{
			glm::mat4 matrix(1.0f);
			for (int column = 0; column < 4; ++column)
				for (int row = 0; row < 4; ++row)
					matrix[column][row] =
						static_cast<float>(node.matrix[static_cast<size_t>(column * 4 + row)]);

			return decomposeTransform(matrix);
		}

		if (node.translation.size() == 3)
			transform.translation =
				glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
		if (node.rotation.size() == 4)
			transform.rotation = glm::quat(
				static_cast<float>(node.rotation[3]),
				static_cast<float>(node.rotation[0]),
				static_cast<float>(node.rotation[1]),
				static_cast<float>(node.rotation[2]));
		if (node.scale.size() == 3)
			transform.scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
		return transform;
	}

	std::vector<float>
	readFloatAccessor(const tinygltf::Model& model, int accessorIndex, int& components)
	{
		components = 0;
		if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size())
			return {};

		const auto& accessor = model.accessors[static_cast<size_t>(accessorIndex)];
		if (accessor.sparse.isSparse)
			throw std::runtime_error("bmesh: sparse accessors are not supported");
		if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
			throw std::runtime_error("bmesh: only float accessors are supported here");
		if (accessor.bufferView < 0)
			return {};

		components = tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type));

		const auto&  view   = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
		const auto&  buffer = model.buffers[static_cast<size_t>(view.buffer)];
		const size_t packed = static_cast<size_t>(components) * sizeof(float);
		const size_t stride = view.byteStride != 0 ? view.byteStride : packed;
		const auto*  base   = reinterpret_cast<const std::byte*>(buffer.data.data()) +
		                      view.byteOffset + accessor.byteOffset;

		std::vector<float> out(accessor.count * static_cast<size_t>(components));
		for (size_t i = 0; i < accessor.count; ++i)
			std::memcpy(
				out.data() + i * static_cast<size_t>(components),
				base + i * stride,
				packed);

		return out;
	}

	std::vector<uint32_t>
	buildNodeParents(const tinygltf::Model& model)
	{
		std::vector<uint32_t> parents(model.nodes.size(), c_InvalidIndex);
		for (size_t i = 0; i < model.nodes.size(); ++i)
			for (const int child : model.nodes[i].children)
				if (child >= 0 && static_cast<size_t>(child) < parents.size())
					parents[static_cast<size_t>(child)] = static_cast<uint32_t>(i);

		return parents;
	}
}
