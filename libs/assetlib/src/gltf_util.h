#pragma once
#include <assetlib_structs/Node.h>

#include <tiny_gltf.h>

namespace assetlib
{
	/** The rotation/scale split of a node matrix. Shear is not representable and is dropped. */
	[[nodiscard]] Transform
	decomposeTransform(const glm::mat4& matrix) noexcept;

	/** A node's local transform, taken from its TRS or decomposed from its matrix. */
	[[nodiscard]] Transform
	readNodeTransform(const tinygltf::Node& node) noexcept;

	/**
	 * A float accessor's values, flattened: `count * components`, with the buffer view's stride
	 * resolved.
	 *
	 * @param components Receives the number of components per element (3 for VEC3, 16 for MAT4, ...).
	 * @throws std::runtime_error if the accessor is sparse or does not hold floats.
	 */
	[[nodiscard]] std::vector<float>
	readFloatAccessor(const tinygltf::Model& model, int accessorIndex, int& components);

	/** Each node's parent, or c_InvalidIndex. glTF stores only the other direction. */
	[[nodiscard]] std::vector<uint32_t>
	buildNodeParents(const tinygltf::Model& model);
}
