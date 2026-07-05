#include <assetlib/bmesh_io.h>

using namespace assetlib;

namespace
{
	BMesh
	makeSampleMesh()
	{
		BMesh mesh;
		mesh.stringPool.push_back('\0');

		Node root{};
		root.localTransform = { glm::vec3(0.0f),
			                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                    glm::vec3(1.0f) };
		root.parent         = c_InvalidIndex;
		root.firstChild     = 1;
		root.nextSibling    = c_InvalidIndex;
		root.mesh           = 0;
		root.nameOffset     = 0;

		Node child{};
		child.localTransform = { glm::vec3(1.0f, 2.0f, 3.0f),
			                     glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                     glm::vec3(1.0f) };
		child.parent         = 0;
		child.firstChild     = c_InvalidIndex;
		child.nextSibling    = c_InvalidIndex;
		child.mesh           = c_InvalidIndex;
		child.nameOffset     = 0;

		mesh.nodes = { root, child };
		mesh.roots = { 0 };

		Submesh submesh{};
		submesh.vertexByteOffset = 0;
		submesh.vertexCount      = 3;
		submesh.indexByteOffset  = 0;
		submesh.indexCount       = 3;
		submesh.indexType        = IndexType::kUint16;
		submesh.firstMeshlet     = 0;
		submesh.meshletCount     = 1;
		submesh.material         = 0;
		submesh.aabbMin          = glm::vec3(0.0f);
		submesh.aabbMax          = glm::vec3(1.0f);
		mesh.submeshes           = { submesh };

		mesh.meshes = { Mesh{ 0, 1, 0 } };

		Meshlet meshlet{};
		meshlet.vertexOffset   = 0;
		meshlet.triangleOffset = 0;
		meshlet.vertexCount    = 3;
		meshlet.triangleCount  = 1;
		meshlet.boundingCenter = glm::vec3(0.5f, 0.5f, 0.0f);
		meshlet.boundingRadius = 1.0f;
		mesh.meshlets          = { meshlet };
		mesh.meshletVertices   = { 0, 1, 2 };
		mesh.meshletTriangles  = { 0, 1, 2 };

		mesh.vertexData.resize(3 * 48, std::byte{ 0x7 });
		mesh.indexData = { std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 },
			               std::byte{ 0 }, std::byte{ 2 }, std::byte{ 0 } };

		mesh.materials = { "mat0.bmaterial", "mat1.bmaterial" };
		return mesh;
	}
}

TEST_CASE("serialize/deserialize round-trips every pool", "[bmesh][io]")
{
	const auto original = makeSampleMesh();
	const auto bytes    = serialize(original);
	const auto restored = deserialize(bytes);

	REQUIRE(restored.nodes.size() == original.nodes.size());
	REQUIRE(restored.meshes.size() == original.meshes.size());
	REQUIRE(restored.submeshes.size() == original.submeshes.size());
	REQUIRE(restored.meshlets.size() == original.meshlets.size());
	REQUIRE(restored.meshletVertices == original.meshletVertices);
	REQUIRE(restored.meshletTriangles == original.meshletTriangles);
	REQUIRE(restored.vertexData == original.vertexData);
	REQUIRE(restored.indexData == original.indexData);
	REQUIRE(restored.stringPool == original.stringPool);
	REQUIRE(restored.roots == original.roots);
	REQUIRE(restored.materials == original.materials);
	REQUIRE(restored.nodes[0].firstChild == 1);
	REQUIRE(restored.submeshes[0].vertexCount == 3);

	// A re-serialize of the restored mesh must be byte-identical.
	REQUIRE(serialize(restored) == bytes);
}

TEST_CASE("deserialize rejects a corrupt magic", "[bmesh][io]")
{
	auto bytes = serialize(makeSampleMesh());
	bytes[0]   = std::byte{ 0xFF };
	REQUIRE_THROWS_AS(deserialize(bytes), std::runtime_error);
}

TEST_CASE("deserialize rejects a truncated stream", "[bmesh][io]")
{
	const auto                       bytes = serialize(makeSampleMesh());
	const std::span<const std::byte> truncated(bytes.data(), bytes.size() / 2);
	REQUIRE_THROWS_AS(deserialize(truncated), std::runtime_error);
}

TEST_CASE("save then load reproduces the mesh on disk", "[bmesh][io]")
{
	const auto original = makeSampleMesh();
	const auto path     = std::filesystem::temp_directory_path() / "bmesh_container_test.bmesh";

	save(original, path);
	const auto restored = load(path);
	std::filesystem::remove(path);

	REQUIRE(restored.nodes.size() == original.nodes.size());
	REQUIRE(restored.vertexData == original.vertexData);
	REQUIRE(restored.materials == original.materials);
	REQUIRE(serialize(restored) == serialize(original));
}
