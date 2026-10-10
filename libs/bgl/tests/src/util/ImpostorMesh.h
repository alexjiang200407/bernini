#pragma once
#include <array>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/glm.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// A 2 x 2 quad drawn unlit white, with an impostor written by hand rather than baked from it --
// every frame a red disc half the frame across, facing the viewer -- so a frame tells the mesh,
// the impostor and nothing apart. For the suites that draw an impostor without a cook in the way.

namespace bgl::test
{
	inline void
	AppendQuad(assetlib::BMesh& mesh)
	{
		const std::array<glm::vec3, 4> corners = {
			glm::vec3(-1.0f, -1.0f, 0.0f),
			glm::vec3(1.0f, -1.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 0.0f),
			glm::vec3(-1.0f, 1.0f, 0.0f),
		};
		mesh.vertexData.resize(sizeof(corners));
		std::memcpy(mesh.vertexData.data(), corners.data(), sizeof(corners));

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexCount    = 4;
		meshlet.triangleCount  = 2;
		meshlet.boundingRadius = 2.0f;
		mesh.meshlets.push_back(meshlet);
		mesh.meshletVertices  = { 0, 1, 2, 3 };
		mesh.meshletTriangles = { 0, 1, 2, 0, 2, 3 };

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 1;
		submesh.layout.stride         = 12;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.vertexCount           = 4;
		submesh.meshletCount          = 1;
		submesh.material              = assetlib::c_InvalidIndex;
		submesh.aabbMin               = glm::vec3(-1.0f, -1.0f, 0.0f);
		submesh.aabbMax               = glm::vec3(1.0f, 1.0f, 0.0f);
		mesh.submeshes.push_back(submesh);
	}

	/** Every frame of every mip a red disc facing +z, half the frame across, at the sphere's centre. */
	inline void
	AppendDiscImpostor(assetlib::BMesh& mesh)
	{
		mesh.impostors.texels.resize(2 * size_t{ assetlib::c_ImpostorAtlasBytes });
		uint8_t* albedo      = mesh.impostors.texels.data();
		uint8_t* normalDepth = albedo + assetlib::c_ImpostorAtlasBytes;
		size_t   offset      = 0;
		for (uint32_t mip = 0; mip < assetlib::c_ImpostorAtlasMips; ++mip)
		{
			const uint32_t side  = assetlib::c_ImpostorAtlasTexels >> mip;
			const uint32_t frame = assetlib::c_ImpostorFrameTexels >> mip;
			for (uint32_t y = 0; y < side; ++y)
			{
				for (uint32_t x = 0; x < side; ++x)
				{
					const float u =
						(static_cast<float>(x % frame) + 0.5f) / static_cast<float>(frame);
					const float v =
						(static_cast<float>(y % frame) + 0.5f) / static_cast<float>(frame);
					const bool   in    = std::hypot(u * 2.0f - 1.0f, v * 2.0f - 1.0f) < 0.5f;
					const size_t i     = offset + (static_cast<size_t>(y) * side + x) * 4;
					albedo[i]          = 255;
					albedo[i + 1]      = 0;
					albedo[i + 2]      = 0;
					albedo[i + 3]      = in ? 255 : 0;
					normalDepth[i]     = 128;
					normalDepth[i + 1] = 128;
					normalDepth[i + 2] = 255;
					normalDepth[i + 3] = 128;
				}
			}
			offset += static_cast<size_t>(side) * side * 4;
		}
		mesh.impostors.records = { assetlib::MeshImpostor{ .mesh         = 0,
			                                               .albedoOffset = 0,
			                                               .normalDepthOffset =
			                                                   assetlib::c_ImpostorAtlasBytes,
			                                               .minPixels = 0.0f,
			                                               .center    = glm::vec3(0.0f),
			                                               .radius    = 1.0f } };
	}

	/** One level drawn down to `lastFloor` pixels, past which the geom draws its impostor, if any. */
	inline assetlib::BMesh
	MakeDiscImpostorMesh(const bool impostor, const float lastFloor)
	{
		auto mesh = assetlib::BMesh();
		AppendQuad(mesh);
		mesh.meshes.push_back(
			assetlib::Mesh{ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0, .lodCount = 1 });
		mesh.lods = { { lastFloor } };
		if (impostor)
		{
			AppendDiscImpostor(mesh);
		}
		return mesh;
	}

}
