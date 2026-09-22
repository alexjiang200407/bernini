#include <assetlib/bmesh.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/codecs.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Mesh.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>

// The static tier dispatches a whole group of meshlets when the group's cooked sphere meets the
// frustum, and nothing else asks about the meshlets under it. So a vertex outside that sphere is a
// triangle the renderer may never launch -- a hole in the picture that no image the cook produces
// would show.

using namespace assetlib;

namespace
{
	glm::vec3
	PositionAt(const imp::BMeshImport& mesh, const Submesh& submesh, uint32_t vertexIndex)
	{
		const size_t offset =
			submesh.vertexByteOffset + static_cast<size_t>(vertexIndex) * submesh.layout.stride;

		auto position = glm::vec3();
		std::memcpy(&position, mesh.vertexData.data() + offset, sizeof(position));
		return position;
	}
}

TEST_CASE("Every cooked group bound encloses the meshlets under it", "[bmesh][meshletgroups]")
{
	const std::filesystem::path path = "assets/suzanne.glb";
	REQUIRE(std::filesystem::exists(path));

	const auto mesh = loadFromGltf(path);
	REQUIRE(!mesh.submeshes.empty());

	uint32_t groupsChecked = 0;

	for (const Submesh& submesh : mesh.submeshes)
	{
		const uint32_t groupCount =
			(submesh.meshletCount + c_MeshletsPerGroup - 1u) / c_MeshletsPerGroup;

		// The renderer derives a submesh's group count from its meshlet count alone, so a cook that
		// rounded differently would read another submesh's bounds.
		REQUIRE(submesh.firstMeshletGroup + groupCount <= mesh.meshletGroups.size());

		for (uint32_t m = 0; m < submesh.meshletCount; ++m)
		{
			const Meshlet&      meshlet = mesh.meshlets[submesh.firstMeshlet + m];
			const MeshletGroup& group =
				mesh.meshletGroups[submesh.firstMeshletGroup + m / c_MeshletsPerGroup];

			for (uint32_t v = 0; v < meshlet.vertexCount; ++v)
			{
				const glm::vec3 position =
					PositionAt(mesh, submesh, mesh.meshletVertices[meshlet.vertexOffset + v]);

				const float distance = glm::distance(position, group.boundingCenter);

				INFO(
					"meshlet " << m << " vertex " << v << " at " << distance << ", group radius "
							   << group.boundingRadius);
				// A fit is a float computation, so a vertex may land on the surface a rounding
				// either side of it; a sphere that misses by more than that misses geometry.
				CHECK(distance <= group.boundingRadius * 1.0001f);
			}
		}

		groupsChecked += groupCount;
	}

	// The premise: the mesh has groups, and more than one, so the loop above compared a meshlet
	// against a bound fitted to other meshlets too.
	INFO("groups " << groupsChecked << " over " << mesh.meshlets.size() << " meshlets");
	REQUIRE(groupsChecked > 1u);
	REQUIRE(groupsChecked == mesh.meshletGroups.size());
}

TEST_CASE("Group bounds survive a .bmesh round-trip", "[bmesh][meshletgroups][io]")
{
	const auto mesh = loadFromGltf(std::filesystem::path("assets/suzanne.glb"));
	REQUIRE(!mesh.meshletGroups.empty());

	const BMesh written  = toBMesh(mesh);
	const BMesh restored = AssetCodec<BMesh>::Deserialize(AssetCodec<BMesh>::Serialize(written));

	REQUIRE(restored.meshletGroups.size() == mesh.meshletGroups.size());
	for (size_t g = 0; g < restored.meshletGroups.size(); ++g)
	{
		CHECK(restored.meshletGroups[g].boundingCenter == mesh.meshletGroups[g].boundingCenter);
		CHECK(restored.meshletGroups[g].boundingRadius == mesh.meshletGroups[g].boundingRadius);
	}

	REQUIRE(restored.submeshes.size() == mesh.submeshes.size());
	for (size_t s = 0; s < restored.submeshes.size(); ++s)
	{
		CHECK(restored.submeshes[s].firstMeshletGroup == mesh.submeshes[s].firstMeshletGroup);
	}
}
