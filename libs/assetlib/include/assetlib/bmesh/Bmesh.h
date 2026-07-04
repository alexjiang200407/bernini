#pragma once
#include <assetlib/bmesh/Mesh.h>
#include <assetlib/bmesh/Node.h>
#include <assetlib/bmesh/Texture.h>

namespace assetlib::bmesh
{
	inline constexpr uint32_t c_InvalidIndex = 0xFFFFFFFFu;

	/**
	 * The in-memory Bernini mesh.
	 */
	struct BMesh
	{
		std::vector<Node>     nodes;
		std::vector<uint32_t> roots;  // node indices whose parent == c_InvalidIndex
		std::vector<Mesh>     meshes;
		std::vector<Submesh>  submeshes;

		std::vector<Meshlet>  meshlets;
		std::vector<uint32_t> meshletVertices;   // meshopt vertex remap
		std::vector<uint8_t>  meshletTriangles;  // meshopt local indices, 3 per triangle

		std::vector<std::byte> vertexData;  // all interleaved vertex blobs
		std::vector<std::byte> indexData;   // all index buffers
		std::vector<char>      stringPool;  // NUL-terminated names; offset 0 is the empty string

		std::vector<Texture> textures;  // detached; not serialized into the container
	};
}
