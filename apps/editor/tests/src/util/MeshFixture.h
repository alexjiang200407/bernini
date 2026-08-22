#pragma once

#include <assetlib/bmesh_io.h>
#include <assetlib_structs/BMesh.h>

// A `.bmesh` with real geometry in it, which is what separates these fixtures from the ones that
// only need a shape: anything going through `bgl::CookStaticMesh` reads the meshlet streams, and a
// submesh with no vertices is refused rather than cooked.

namespace editor::test
{
	/** Appends one mesh entry of `submeshCount` single-triangle submeshes. Its index. */
	inline uint32_t
	AddTriangleMesh(assetlib::BMesh& mesh, uint32_t submeshCount, bool skinned = false)
	{
		constexpr uint16_t c_Stride = 12;  // one float32x3 position

		const auto firstSubmesh = static_cast<uint32_t>(mesh.submeshes.size());

		for (uint32_t s = 0; s < submeshCount; ++s)
		{
			const auto vertexBase = static_cast<uint32_t>(mesh.vertexData.size());
			mesh.vertexData.resize(vertexBase + 3 * c_Stride);

			const std::array<glm::vec3, 3> positions = { glm::vec3(-1.0f, -1.0f, 0.0f),
				                                         glm::vec3(1.0f, -1.0f, 0.0f),
				                                         glm::vec3(0.0f, 1.0f, 0.0f) };
			std::memcpy(mesh.vertexData.data() + vertexBase, positions.data(), 3 * c_Stride);

			auto meshlet           = assetlib::Meshlet();
			meshlet.vertexOffset   = static_cast<uint32_t>(mesh.meshletVertices.size());
			meshlet.triangleOffset = static_cast<uint32_t>(mesh.meshletTriangles.size());
			meshlet.vertexCount    = 3;
			meshlet.triangleCount  = 1;
			meshlet.boundingCenter = glm::vec3(0.0f);
			meshlet.boundingRadius = 2.0f;

			const auto firstMeshlet = static_cast<uint32_t>(mesh.meshlets.size());
			mesh.meshlets.push_back(meshlet);

			for (uint32_t v = 0; v < 3; ++v) mesh.meshletVertices.push_back(v);
			for (uint8_t t = 0; t < 3; ++t) mesh.meshletTriangles.push_back(t);

			auto submesh                  = assetlib::Submesh();
			submesh.layout.stride         = c_Stride;
			submesh.layout.attributeCount = skinned ? 2 : 1;
			submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
				                              assetlib::VertexFormat::kFloat32x3,
				                              0 };
			if (skinned)
				submesh.layout.attributes[1] = { assetlib::VertexSemantic::kJoints0,
					                             assetlib::VertexFormat::kUint16x4,
					                             0 };

			submesh.vertexByteOffset = vertexBase;
			submesh.vertexCount      = 3;
			submesh.firstMeshlet     = firstMeshlet;
			submesh.meshletCount     = 1;
			submesh.material         = assetlib::c_InvalidIndex;
			submesh.aabbMin          = glm::vec3(-1.0f);
			submesh.aabbMax          = glm::vec3(1.0f);
			submesh.nameOffset       = 0;
			mesh.submeshes.push_back(submesh);
		}

		mesh.meshes.push_back(
			{ .firstSubmesh = firstSubmesh, .submeshCount = submeshCount, .nameOffset = 0 });

		return static_cast<uint32_t>(mesh.meshes.size() - 1);
	}

	/** Appends a root node instancing `meshIndex`, translated along +X by `x`. */
	inline void
	AddMeshNode(assetlib::BMesh& mesh, uint32_t meshIndex, float x = 0.0f)
	{
		auto node           = assetlib::Node();
		node.localTransform = { glm::vec3(x, 0.0f, 0.0f),
			                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                    glm::vec3(1.0f) };
		node.parent         = assetlib::c_InvalidIndex;
		node.firstChild     = assetlib::c_InvalidIndex;
		node.nextSibling    = assetlib::c_InvalidIndex;
		node.mesh           = meshIndex;
		node.nameOffset     = 0;

		mesh.roots.push_back(static_cast<uint32_t>(mesh.nodes.size()));
		mesh.nodes.push_back(node);
	}
}
