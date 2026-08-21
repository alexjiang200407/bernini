#pragma once
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <core/str/string_pool.h>

#include <assetlib_structs/SourceRef.h>

namespace assetlib
{
	/**
	 * A mesh loaded from a `.bmesh` file: the modular, path-referencing counterpart of
	 * imp::BMeshImport. The geometry (nodes, meshes, submeshes, meshlets and the vertex/index/string
	 * pools) is identical to the import form, but materials are not embedded -- they are referenced by
	 * file path so textures and materials live as standalone, shareable assets. Baking an
	 * imp::BMeshImport emits one `.bmesh` (this struct) plus the referenced texture / material /
	 * animation files.
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
		core::string_pool      stringPool;

		std::vector<std::string> materials;

		std::string skeleton;  // .bskel the joint indices address; empty for a static mesh

		SourceRef source;  // the copied .glb this was derived from; empty key when never recorded
	};
}
