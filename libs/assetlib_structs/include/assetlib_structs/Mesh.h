#pragma once
#include <assetlib_structs/VertexLayout.h>
#include <core/glm.h>

namespace assetlib
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
		uint32_t  vertexOffset;    // into BMeshImport::meshletVertices
		uint32_t  triangleOffset;  // into BMeshImport::meshletTriangles
		uint32_t  vertexCount;
		uint32_t  triangleCount;
		glm::vec3 boundingCenter;
		float     boundingRadius;
	};

	static_assert(sizeof(Meshlet) == 32);

	/**
	 * One drawable primitive. Vertex/index bytes live in the document pools; ranges reference them.
	 *
	 * The triangles are stored twice. `firstMeshlet`/`meshletCount` is what bgl draws; the plain
	 * `indexByteOffset`/`indexCount` range is read by cook-time tangent generation and by
	 * `assetlib_cli`'s `describe` and raw-OBJ export, and is what a renderer with no mesh-shader
	 * stage would draw. No renderer reads it, so it profiles as cook-size overhead -- deleting it
	 * breaks those three and costs an `AssetCodec<BMesh>::c_BakeToken` bump plus a re-cook of every
	 * asset. `vertexByteOffset`/`vertexCount` is not duplicated: the meshlet arrays index into the
	 * same `vertexData`, so bgl and gamelib both read it.
	 */
	struct Submesh
	{
		VertexLayout layout;
		uint32_t     vertexByteOffset;  // into BMeshImport::vertexData
		uint32_t     vertexCount;
		uint32_t     indexByteOffset;  // into BMeshImport::indexData
		uint32_t     indexCount;
		IndexType    indexType;
		uint32_t     firstMeshlet;  // range into BMeshImport::meshlets
		uint32_t     meshletCount;
		uint32_t     material;
		glm::vec3    aabbMin;
		glm::vec3    aabbMax;
		uint32_t     nameOffset;
	};

	static_assert(sizeof(Submesh) == 96);

	struct Mesh
	{
		uint32_t firstSubmesh;  // range into BMeshImport::submeshes
		uint32_t submeshCount;
		uint32_t nameOffset;  // into BMeshImport::stringPool
	};

	static_assert(sizeof(Mesh) == 12);
}
