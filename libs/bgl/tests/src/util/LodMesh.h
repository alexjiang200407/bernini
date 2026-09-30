#pragma once
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/glm.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// A mesh with levels of detail assembled in memory, for the suites that upload, cull and draw one
// without a cook in the way.

namespace bgl::test
{
	/** One submesh entry of the in-memory mesh: its triangle count and its box. */
	struct EntrySpec
	{
		uint32_t  triangles;
		glm::vec3 aabbMin;
		glm::vec3 aabbMax;
	};

	/**
	 * One mesh of `submeshCount` submeshes over `entries.size() / submeshCount` levels, every
	 * triangle its own meshlet, and `lods` as its thresholds (none when empty).
	 */
	inline assetlib::BMesh
	MakeLodMesh(std::span<const EntrySpec> entries, uint32_t submeshCount, std::vector<float> lods)
	{
		constexpr uint16_t c_Stride = 12;

		auto     mesh          = assetlib::BMesh();
		uint32_t totalVertices = 0;
		for (const EntrySpec& entry : entries) totalVertices += entry.triangles * 3;
		mesh.vertexData.resize(static_cast<size_t>(totalVertices) * c_Stride);

		uint32_t vertexCursor = 0;
		for (const EntrySpec& spec : entries)
		{
			const auto firstMeshlet = static_cast<uint32_t>(mesh.meshlets.size());
			for (uint32_t i = 0; i < spec.triangles; ++i)
			{
				auto meshlet           = assetlib::Meshlet();
				meshlet.vertexOffset   = static_cast<uint32_t>(mesh.meshletVertices.size());
				meshlet.triangleOffset = static_cast<uint32_t>(mesh.meshletTriangles.size());
				meshlet.vertexCount    = 3;
				meshlet.triangleCount  = 1;
				meshlet.boundingRadius = 1.0f;
				mesh.meshlets.push_back(meshlet);
				for (uint32_t v = 0; v < 3; ++v) mesh.meshletVertices.push_back(i * 3 + v);
				for (uint8_t t = 0; t < 3; ++t) mesh.meshletTriangles.push_back(t);
			}

			auto submesh                  = assetlib::Submesh();
			submesh.layout.attributeCount = 1;
			submesh.layout.stride         = c_Stride;
			submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
				                              assetlib::VertexFormat::kFloat32x3,
				                              0 };
			submesh.vertexByteOffset      = vertexCursor * c_Stride;
			submesh.vertexCount           = spec.triangles * 3;
			submesh.firstMeshlet          = firstMeshlet;
			submesh.meshletCount          = spec.triangles;
			submesh.material              = 0;
			submesh.aabbMin               = spec.aabbMin;
			submesh.aabbMax               = spec.aabbMax;
			mesh.submeshes.push_back(submesh);

			vertexCursor += spec.triangles * 3;
		}

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = submeshCount;
		entry.lodCount     = static_cast<uint32_t>(entries.size()) / submeshCount;
		entry.firstLod     = 0;
		mesh.meshes.push_back(entry);

		for (const float minPixels : lods) mesh.lods.push_back({ minPixels });
		return mesh;
	}

}
