#pragma once
#include <assetlib_structs/VertexLayout.h>
#include <core/glm.h>
#include <cstdint>

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
	 * Meshlets one cooked group bound covers. A run of this many consecutive meshlets of one
	 * submesh, so a submesh's last group is short when its meshlet count is not a multiple.
	 *
	 * The renderer names the same number as `idl::cMeshletsPerGroup`, and reads a submesh's group
	 * count off it -- `bgl` does not link `assetlib`, so the two cannot be shared; a static_assert
	 * in `Scene.cpp`, the one file that sees both, holds them equal.
	 */
	constexpr uint32_t c_MeshletsPerGroup = 8;

	/**
	 * A bounding sphere over one run of `c_MeshletsPerGroup` meshlets, enclosing every vertex every
	 * one of them draws. The static tier's amplification stage tests these instead of the meshlet
	 * spheres, so it reads an eighth as many.
	 */
	struct MeshletGroup
	{
		glm::vec3 boundingCenter;
		float     boundingRadius;
	};

	static_assert(sizeof(MeshletGroup) == 16);

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
		// Range into BMeshImport::meshletGroups; its length is meshletCount / c_MeshletsPerGroup,
		// rounded up.
		uint32_t  firstMeshletGroup;
		uint32_t  material;
		glm::vec3 aabbMin;
		glm::vec3 aabbMax;
		uint32_t  nameOffset;
	};

	static_assert(sizeof(Submesh) == 100);

	/**
	 * One level of detail of a mesh, in `BMesh::lods`. A level is drawn while the placement's
	 * projected diameter, in pixels, is at least `minPixels`, walked from level 0: a size below
	 * every level's draws nothing, so the last level's `minPixels` is the draw-nothing size and 0
	 * there means the mesh is never dropped. Non-increasing across a mesh's levels.
	 */
	struct MeshLod
	{
		float minPixels;
	};

	static_assert(sizeof(MeshLod) == 4);

	/**
	 * Levels one mesh may carry, level 0 included. The renderer names the same number as
	 * `idl::cMaxMeshLods` -- the length of its per-geom table -- and a static_assert in
	 * `Scene_Geometry.cpp`
	 * holds them equal; the document and the cook refuse a source past it.
	 */
	constexpr uint32_t c_MaxMeshLods = 8;

	/**
	 * `submeshCount` is one level's, and the submesh range spans `lodCount` of them, level-major:
	 * level 0's submeshes first in source order, then level 1's in the same order, so
	 * `firstSubmesh + lod * submeshCount + s` is submesh `s` of level `lod`. Every reader that walks
	 * `firstSubmesh .. + submeshCount` therefore sees level 0 and only the upload sees them all. A
	 * level's entry carries the same `nameOffset` and `material` as its level-0 sibling.
	 *
	 * `lodCount` levels of `BMesh::lods` start at `firstLod`. An empty `BMesh::lods` is one level
	 * drawn at every size, whatever `firstLod` says: it is read only when the table is there.
	 */
	struct Mesh
	{
		uint32_t firstSubmesh;  // range into BMeshImport::submeshes
		uint32_t submeshCount;
		uint32_t nameOffset;  // into BMeshImport::stringPool
		uint32_t lodCount = 1;
		uint32_t firstLod = 0;  // range into BMeshImport::lods
	};

	static_assert(sizeof(Mesh) == 20);
}
