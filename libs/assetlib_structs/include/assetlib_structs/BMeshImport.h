#pragma once
#include <assetlib_structs/BMaterialImport.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>

namespace assetlib::imp
{
	/**
	 * The flattened result of importing a source asset (e.g. glTF): every resource is present inline,
	 * with materials indexing directly into `textures`. This is the counterpart of the modular BMesh,
	 * which the importer bakes this into -- replacing the inline textures and materials with file-path
	 * references. See toBMesh / writeTextures in bmesh_io.
	 */
	struct BMeshImport
	{
		std::vector<Node>            nodes;
		std::vector<uint32_t>        roots;  // node indices whose parent == c_InvalidIndex
		std::vector<Mesh>            meshes;
		std::vector<Submesh>         submeshes;
		std::vector<BMaterialImport> materials;

		std::vector<Meshlet>  meshlets;
		std::vector<uint32_t> meshletVertices;   // meshopt vertex remap
		std::vector<uint8_t>  meshletTriangles;  // meshopt local indices, 3 per triangle

		std::vector<std::byte> vertexData;  // all interleaved vertex blobs
		std::vector<std::byte> indexData;   // all index buffers
		std::vector<char>      stringPool;  // NUL-terminated names; offset 0 is the empty string

		std::vector<ImageData> textures;
	};
}
