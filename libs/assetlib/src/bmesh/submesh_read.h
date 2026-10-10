#pragma once
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace assetlib
{
	/**
	 * The submesh's triangles, as vertex indices local to it, out of a mesh's plain index pool.
	 * Empty when it carries no index buffer.
	 *
	 * @throws std::runtime_error if its range runs past `indexData`: the offset and count are the
	 *         file's claim about the pool, and a .bmesh can be named by hand.
	 */
	[[nodiscard]] std::vector<uint32_t>
	readSubmeshIndices(std::span<const std::byte> indexData, const Submesh& submesh);

	/** A float attribute of one vertex, read straight out of the interleaved pool. @pre in range. */
	[[nodiscard]] const float*
	floatsAt(
		std::span<const std::byte> vertexData,
		const Submesh&             submesh,
		const VertexAttribute&     attribute,
		uint32_t                   vertex) noexcept;
}
