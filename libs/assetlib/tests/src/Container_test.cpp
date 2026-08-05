#include <assetlib/bmesh_io.h>
#include <assetlib_structs/BMesh.h>

using namespace assetlib;

namespace
{
	BMesh
	MakeSampleMesh()
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
	const auto original = MakeSampleMesh();
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
	auto bytes = serialize(MakeSampleMesh());
	bytes[0]   = std::byte{ 0xFF };
	REQUIRE_THROWS_AS(deserialize(bytes), std::runtime_error);
}

TEST_CASE("deserialize rejects a truncated stream", "[bmesh][io]")
{
	const auto                       bytes = serialize(MakeSampleMesh());
	const std::span<const std::byte> truncated(bytes.data(), bytes.size() / 2);
	REQUIRE_THROWS_AS(deserialize(truncated), std::runtime_error);
}

namespace
{
	// The container's header and chunk table are private to bmesh_io.cpp, so the corruption tests
	// below hand-decode them: chunkCount at 12 and chunkTableOffset at 16 of the header, then entries
	// of { id, elementSize, offset, byteSize }.
	constexpr size_t c_ChunkCountField       = 12;
	constexpr size_t c_ChunkTableOffsetField = 16;
	constexpr size_t c_EntryOffsetField      = 8;
	constexpr size_t c_EntryByteSizeField    = 16;

	template <typename T>
	T
	FieldAt(const std::vector<std::byte>& bytes, size_t offset)
	{
		T value;
		std::memcpy(&value, bytes.data() + offset, sizeof(T));
		return value;
	}

	template <typename T>
	void
	SetFieldAt(std::vector<std::byte>& bytes, size_t offset, T value)
	{
		std::memcpy(bytes.data() + offset, &value, sizeof(T));
	}

	std::filesystem::path
	WriteTemp(const std::vector<std::byte>& bytes, const char* stem)
	{
		const auto    path = std::filesystem::temp_directory_path() / stem;
		std::ofstream out(path, std::ios::binary);
		out.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		return path;
	}
}

// A chunk's offset and size are both uint64_t straight out of the file, so a bound written as
// `offset + byteSize > size` wraps: this entry passes it while naming a region nowhere near the
// stream, and the copy that follows reads from a wild pointer.
TEST_CASE("deserialize rejects a chunk whose offset and size wrap", "[bmesh][io]")
{
	auto bytes = serialize(makeSampleMesh());

	const auto entry    = FieldAt<uint32_t>(bytes, c_ChunkTableOffsetField);
	const auto byteSize = FieldAt<uint64_t>(bytes, entry + c_EntryByteSizeField);
	REQUIRE(byteSize > 0);

	// offset + byteSize == 2^64 == 0, i.e. under any size the stream can have.
	SetFieldAt<uint64_t>(bytes, entry + c_EntryOffsetField, ~byteSize + 1);

	REQUIRE_THROWS_AS(deserialize(bytes), std::runtime_error);
}

// The chunk table is sized from a count in the file, so it has to be bounded against the file before
// it is allocated rather than after. Rejecting it is not enough to prove that: this count reserves
// 96 GB on the way to being rejected, which takes half a minute and swaps the machine out. Hence the
// clock -- the bound is 5000x the time the check itself needs, so only a re-ordering can trip it.
TEST_CASE("loadMaterialPaths rejects a chunk count without allocating from it", "[bmesh][io]")
{
	auto bytes = serialize(makeSampleMesh());
	SetFieldAt<uint32_t>(bytes, c_ChunkCountField, 0xFFFFFFFFu);

	const auto path  = WriteTemp(bytes, "bmesh_chunk_count_test.bmesh");
	const auto start = std::chrono::steady_clock::now();

	REQUIRE_THROWS_AS(loadMaterialPaths(path), std::runtime_error);

	const auto elapsed = std::chrono::steady_clock::now() - start;
	CHECK(elapsed < std::chrono::seconds(5));

	std::filesystem::remove(path);
}

TEST_CASE("save then load reproduces the mesh on disk", "[bmesh][io]")
{
	const auto original = MakeSampleMesh();
	const auto path     = std::filesystem::temp_directory_path() / "bmesh_container_test.bmesh";

	save(original, path);
	const auto restored = load(path);
	std::filesystem::remove(path);

	REQUIRE(restored.nodes.size() == original.nodes.size());
	REQUIRE(restored.vertexData == original.vertexData);
	REQUIRE(restored.materials == original.materials);
	REQUIRE(serialize(restored) == serialize(original));
}

namespace
{
	// A mesh with `submeshMaterials.size()` submeshes, each pointing at the given material slot.
	BMesh
	MakeMaterialMesh(std::vector<uint32_t> submeshMaterials, std::vector<std::string> materials)
	{
		BMesh mesh;
		mesh.materials = std::move(materials);
		for (const uint32_t material : submeshMaterials)
		{
			auto submesh     = Submesh();
			submesh.material = material;
			mesh.submeshes.push_back(submesh);
		}
		return mesh;
	}
}

TEST_CASE("attachMaterial rewrites an unshared slot in place", "[bmesh][material]")
{
	// Submesh 1 is the only user of slot 1, so it may claim it.
	auto mesh = MakeMaterialMesh({ 0, 1 }, { "mat0.bmaterial", "mat1.bmaterial" });

	REQUIRE(attachMaterial(mesh, 1, "authored.bmaterial"));

	REQUIRE(mesh.materials.size() == 2);  // no new slot
	REQUIRE(mesh.materials[1] == "authored.bmaterial");
	REQUIRE(mesh.submeshes[1].material == 1);
	REQUIRE(mesh.materials[0] == "mat0.bmaterial");  // submesh 0 untouched
	REQUIRE(mesh.submeshes[0].material == 0);
}

TEST_CASE("attachMaterial does not repoint siblings sharing a slot", "[bmesh][material]")
{
	// Both submeshes were imported with the same material. Re-materialing one must not change the
	// other -- it gets a slot of its own instead.
	auto mesh = MakeMaterialMesh({ 0, 0 }, { "shared.bmaterial" });

	REQUIRE(attachMaterial(mesh, 0, "authored.bmaterial"));

	REQUIRE(mesh.materials.size() == 2);
	REQUIRE(mesh.materials[mesh.submeshes[0].material] == "authored.bmaterial");
	REQUIRE(mesh.submeshes[1].material == 0);
	REQUIRE(mesh.materials[0] == "shared.bmaterial");
}

TEST_CASE("attachMaterial reuses an existing entry instead of duplicating", "[bmesh][material]")
{
	auto mesh = MakeMaterialMesh({ 0, 0 }, { "shared.bmaterial", "other.bmaterial" });

	REQUIRE(attachMaterial(mesh, 1, "other.bmaterial"));

	REQUIRE(mesh.materials.size() == 2);  // "other" was already there
	REQUIRE(mesh.submeshes[1].material == 1);
	REQUIRE(mesh.submeshes[0].material == 0);
}

TEST_CASE("attachMaterial reports no change when already attached", "[bmesh][material]")
{
	auto mesh = MakeMaterialMesh({ 0 }, { "mat0.bmaterial" });

	// Sole user of the slot, and it already names this material.
	REQUIRE_FALSE(attachMaterial(mesh, 0, "mat0.bmaterial"));

	// Shared slot already naming the material: the submesh stays where it is.
	auto shared = MakeMaterialMesh({ 0, 0 }, { "mat0.bmaterial" });
	REQUIRE_FALSE(attachMaterial(shared, 0, "mat0.bmaterial"));
	REQUIRE(shared.materials.size() == 1);
	REQUIRE(shared.submeshes[0].material == 0);
}

TEST_CASE("attachMaterial gives an unmaterialed submesh a new slot", "[bmesh][material]")
{
	auto mesh = MakeMaterialMesh({ c_InvalidIndex }, {});

	REQUIRE(attachMaterial(mesh, 0, "authored.bmaterial"));

	REQUIRE(mesh.materials.size() == 1);
	REQUIRE(mesh.submeshes[0].material == 0);
}

TEST_CASE("attachMaterial rejects an out-of-range submesh", "[bmesh][material]")
{
	auto mesh = MakeMaterialMesh({ 0 }, { "mat0.bmaterial" });
	REQUIRE_THROWS_AS(attachMaterial(mesh, 1, "authored.bmaterial"), std::runtime_error);
}
