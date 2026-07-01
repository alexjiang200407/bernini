#pragma once
#include <assetlib/bmesh/VertexLayout.h>
#include <bgl/glm.h>

namespace assetlib::bmesh
{
	enum class IndexType : uint8_t
	{
		kNone,
		kUint16,
		kUint32
	};

	/** A meshlet cluster. Deliberately independent of the runtime GPU `bgl::idl::Meshlet`. */
	struct Meshlet
	{
		uint32_t  vertexOffset;    // into BMesh::meshletVertices
		uint32_t  triangleOffset;  // into BMesh::meshletTriangles
		uint32_t  vertexCount;
		uint32_t  triangleCount;
		glm::vec3 boundingCenter;
		float     boundingRadius;
	};

	static_assert(sizeof(Meshlet) == 32);

	/** One drawable primitive. Vertex/index bytes live in the document pools; ranges reference them. */
	struct Submesh
	{
		VertexLayout layout;
		uint32_t     vertexByteOffset;  // into BMesh::vertexData
		uint32_t     vertexCount;
		uint32_t     indexByteOffset;  // into BMesh::indexData
		uint32_t     indexCount;
		IndexType    indexType;
		uint32_t     firstMeshlet;  // range into BMesh::meshlets
		uint32_t     meshletCount;
		uint32_t     material;  // reserved; always c_InvalidIndex for now (materials ignored)
		glm::vec3    aabbMin;
		glm::vec3    aabbMax;
	};

	static_assert(sizeof(Submesh) == 92);

	struct Mesh
	{
		uint32_t firstSubmesh;  // range into BMesh::submeshes
		uint32_t submeshCount;
		uint32_t nameOffset;  // into BMesh::stringPool
	};

	static_assert(sizeof(Mesh) == 12);
}
