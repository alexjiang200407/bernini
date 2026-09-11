#pragma once
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <core/str/string_pool.h>

#include <assetlib_structs/SourceRef.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
		std::vector<std::byte> indexData;   // all index buffers; unread by bgl, see Submesh
		core::string_pool      stringPool;

		std::vector<std::string> materials;

		std::string skeleton;  // .bskel the joint indices address; empty for a static mesh

		/** The rig the joint indices were cooked against -- see assetlib::skeletonSignature. */
		uint64_t skeletonSignature = 0;

		/**
		 * That rig's bone names, in bone order -- what lets a rig that has grown a bone since be
		 * matched to these joint indices by name rather than refused. Empty in a file written
		 * before the list existed, and for a static mesh, which addresses no bone.
		 *
		 * Beside `joints0` and never instead of it: the vertex blob is what the GPU skins from, so
		 * a name is resolved at load and the indices in it are remapped, never replaced.
		 */
		std::vector<std::string> skeletonBoneNames;

		/**
		 * The geometry this file holds, hashed once at cook time -- the vertex blob and the entry
		 * and submesh tables that address it (assetlib::geometrySignature). Zero means "not
		 * recorded": a file written before the field existed, or a mesh whose blob has been
		 * rewritten in memory since it was read, and a reader that finds zero computes it.
		 *
		 * Written by the codec from the bytes it is about to emit, never by a producer, so a
		 * `.bmesh` on disk cannot carry one that disagrees with its own geometry.
		 */
		uint64_t geometrySignature = 0;

		SourceRef source;  // the copied .glb this was derived from; empty key when never recorded
	};
}
