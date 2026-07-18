#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>

#include <catch2/catch_approx.hpp>

using namespace assetlib;
using namespace assetlib::imp;

namespace
{
	// A minimal glTF 2.0 document: one node -> one mesh -> one triangle primitive, buffer inlined as a
	// base64 data URI (3 float3 positions + 3 uint16 indices).
	constexpr const char* c_TriangleGltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0, "name": "tri" } ],
  "meshes": [ { "name": "triangle", "primitives": [
    { "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 } ] } ],
  "buffers": [ { "byteLength": 42,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 6, "target": 34963 } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [ 0, 0, 0 ], "max": [ 1, 1, 0 ] },
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" } ]
})";

	/**
	 * The triangle, plus a material table covering every shading model and alpha mode the importer has
	 * to tell apart. Nothing references them: what a material *is* does not depend on being drawn, and
	 * the primitive's own material index is exercised by the geometry tests above.
	 */
	constexpr const char* c_MaterialsGltf = R"({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ "KHR_materials_unlit", "KHR_materials_pbrSpecularGlossiness" ],
  "materials": [
    { "name": "plain", "pbrMetallicRoughness": { "metallicFactor": 0.25, "roughnessFactor": 0.5 } },
    { "name": "leaves", "alphaMode": "MASK", "alphaCutoff": 0.3 },
    { "name": "glass", "alphaMode": "BLEND" },
    { "name": "sign", "extensions": { "KHR_materials_unlit": {} } },
    { "name": "old", "extensions": { "KHR_materials_pbrSpecularGlossiness": {} } }
  ]
})";

	std::filesystem::path
	writeTempGltf(const char* json = c_TriangleGltf, const char* name = "bmesh_triangle_test.gltf")
	{
		const auto    path = std::filesystem::temp_directory_path() / name;
		std::ofstream out(path, std::ios::binary);
		out << json;
		return path;
	}

	/** The materials document, imported and cleaned up. */
	BMeshImport
	loadMaterialsGltf()
	{
		const auto path = writeTempGltf(c_MaterialsGltf, "bmesh_materials_test.gltf");
		auto       mesh = loadFromGltf(path);
		std::filesystem::remove(path);
		return mesh;
	}
}

TEST_CASE("loadFromGltf imports geometry and hierarchy from a triangle", "[bmesh][gltf]")
{
	const auto path = writeTempGltf();
	const auto mesh = loadFromGltf(path);
	std::filesystem::remove(path);

	REQUIRE(mesh.nodes.size() == 1);
	REQUIRE(mesh.roots == std::vector<uint32_t>{ 0 });
	REQUIRE(mesh.nodes[0].mesh == 0);

	REQUIRE(mesh.meshes.size() == 1);
	REQUIRE(mesh.meshes[0].submeshCount == 1);
	REQUIRE(mesh.submeshes.size() == 1);

	const auto& submesh = mesh.submeshes[0];
	REQUIRE(submesh.vertexCount == 3);
	REQUIRE(submesh.indexCount == 3);
	REQUIRE(submesh.indexType == IndexType::kUint16);

	REQUIRE(submesh.layout.attributeCount == 1);
	REQUIRE(submesh.layout.attributes[0].semantic == VertexSemantic::kPosition);
	REQUIRE(submesh.layout.stride == 12);
	REQUIRE(mesh.vertexData.size() == 3 * 12);
	REQUIRE(submesh.meshletCount >= 1);
	REQUIRE(!mesh.meshlets.empty());

	REQUIRE(submesh.aabbMin.x == Catch::Approx(0.0f));
	REQUIRE(submesh.aabbMax.x == Catch::Approx(1.0f));
	REQUIRE(submesh.aabbMax.y == Catch::Approx(1.0f));
}

TEST_CASE("an imported triangle survives a container round-trip", "[bmesh][gltf][io]")
{
	const auto path = writeTempGltf();
	const auto mesh = loadFromGltf(path);
	std::filesystem::remove(path);

	const auto restored = deserialize(serialize(toBMesh(mesh)));
	REQUIRE(restored.vertexData == mesh.vertexData);
	REQUIRE(restored.submeshes.size() == mesh.submeshes.size());
	REQUIRE(restored.meshlets.size() == mesh.meshlets.size());
}

TEST_CASE("loadFromGltf imports the Suzanne test model", "[bmesh][gltf]")
{
	const std::filesystem::path path = "assets/suzanne.glb";
	REQUIRE(std::filesystem::exists(path));

	const auto mesh = loadFromGltf(path);

	REQUIRE(!mesh.nodes.empty());
	REQUIRE(!mesh.roots.empty());
	REQUIRE(mesh.meshes.size() >= 1);
	REQUIRE(mesh.submeshes.size() >= 1);

	// Every submesh's vertex/index ranges must be consistent, and meshlets must have been built.
	size_t totalVertexBytes = 0;
	for (const auto& submesh : mesh.submeshes)
	{
		REQUIRE(submesh.vertexCount > 0);
		REQUIRE(submesh.indexCount > 0);
		// Stride now reflects the source's actual attributes (>= position); it is no longer a fixed 48.
		REQUIRE(submesh.layout.stride >= 12);
		REQUIRE(submesh.layout.attributes[0].semantic == VertexSemantic::kPosition);
		REQUIRE(submesh.meshletCount >= 1);
		totalVertexBytes += static_cast<size_t>(submesh.vertexCount) * submesh.layout.stride;
	}
	REQUIRE(mesh.vertexData.size() == totalVertexBytes);
	REQUIRE(!mesh.meshlets.empty());
	REQUIRE(!mesh.meshletVertices.empty());
	REQUIRE(!mesh.meshletTriangles.empty());

	// The bounding box must be non-degenerate.
	const auto& first = mesh.submeshes.front();
	REQUIRE(first.aabbMax.x > first.aabbMin.x);
	REQUIRE(first.aabbMax.y > first.aabbMin.y);

	// And it survives a full container round-trip.
	const auto restored = deserialize(serialize(toBMesh(mesh)));
	REQUIRE(restored.vertexData == mesh.vertexData);
	REQUIRE(restored.meshlets.size() == mesh.meshlets.size());
}

TEST_CASE(
	"A glTF's images arrive decoded, and its materials point at them",
	"[bmesh][gltf][textures]")
{
	// The only test model with images; suzanne.glb has none, so nothing else reaches buildTextures.
	const std::filesystem::path glb = "assets/apples.glb";
	REQUIRE(std::filesystem::exists(glb));

	const auto import = loadFromGltf(glb);

	REQUIRE(import.textures.size() == 2);

	for (const ImageData& texture : import.textures)
	{
		REQUIRE(texture.width > 0);
		REQUIRE(texture.height > 0);
		REQUIRE(texture.mipLevels >= 1);
		REQUIRE(texture.subresources.size() == texture.mipLevels);

		// pixels holds the whole mip pyramid, so only the base subresource's size follows from
		// the dimensions.
		const auto& base = texture.subresources.front();
		REQUIRE(base.rowPitch == static_cast<uint64_t>(texture.width) * 4);
		REQUIRE(base.slicePitch == base.rowPitch * texture.height);
		REQUIRE(texture.pixels.size() >= base.offset + base.slicePitch);

		// An allocated-but-unfilled buffer is all zeroes: this is what separates a decode from a
		// no-op.
		REQUIRE(
			std::ranges::any_of(texture.pixels, [](std::byte b) { return b != std::byte{ 0 }; }));
	}

	// A material still pointing at nothing means imageToTexture never got built.
	REQUIRE(std::ranges::any_of(import.materials, [&](const BMaterialImport& material) {
		return material.baseColorTexture < import.textures.size();
	}));
}

TEST_CASE("A glTF's alpha mode and cutoff come across", "[bmesh][gltf]")
{
	const auto mesh = loadMaterialsGltf();
	REQUIRE(mesh.materials.size() == 5);

	// Each of glTF's three alpha modes maps to its own: OPAQUE, MASK (alpha test), BLEND (alpha blend).
	CHECK(mesh.materials[0].alphaMode == AlphaMode::kOpaque);
	CHECK(mesh.materials[1].alphaMode == AlphaMode::kMask);
	CHECK(mesh.materials[1].alphaCutoff == Catch::Approx(0.3f));
	CHECK(mesh.materials[2].alphaMode == AlphaMode::kBlend);

	// glTF's own default, not the engine's: a MASK material that names no cutoff cuts at 0.5.
	CHECK(mesh.materials[0].alphaCutoff == Catch::Approx(0.5f));
}

TEST_CASE("A material declaring another shading model is not PBR", "[bmesh][gltf]")
{
	const auto mesh = loadMaterialsGltf();
	REQUIRE(mesh.materials.size() == 5);

	// Metallic-roughness is glTF's shading model, so a material is PBR unless it says otherwise. The
	// two that do say otherwise carry fields that are glTF's defaults rather than the author's intent,
	// which is why importing them as PBR would be a lie rather than an approximation.
	CHECK(mesh.materials[0].isPbr);
	CHECK(mesh.materials[1].isPbr);
	CHECK(mesh.materials[2].isPbr);
	CHECK_FALSE(mesh.materials[3].isPbr);  // KHR_materials_unlit
	CHECK_FALSE(mesh.materials[4].isPbr);  // KHR_materials_pbrSpecularGlossiness
}

TEST_CASE("probeGltfMaterials counts the PBR materials", "[bmesh][gltf]")
{
	const auto path  = writeTempGltf(c_MaterialsGltf, "bmesh_probe_test.gltf");
	const auto probe = probeGltfMaterials(path);
	std::filesystem::remove(path);

	CHECK(probe.materialCount == 5);
	CHECK(probe.pbrMaterialCount == 3);
}

TEST_CASE("probeGltfMaterials sees what a full import sees", "[bmesh][gltf]")
{
	// The stubbed image loader is the whole point of the probe, and it is also the thing most likely to
	// make it disagree with an import -- a loader that fails rather than no-ops takes the parse down
	// with it. apples.glb is the only fixture with real textures, so it is the only one where the stub
	// is exercised at all.
	const std::filesystem::path glb = "assets/apples.glb";
	REQUIRE(std::filesystem::exists(glb));

	const auto import = loadFromGltf(glb);
	const auto probe  = probeGltfMaterials(glb);

	REQUIRE_FALSE(import.textures.empty());
	CHECK(probe.materialCount == import.materials.size());
	CHECK(
		probe.pbrMaterialCount ==
		static_cast<size_t>(
			std::ranges::count_if(import.materials, [](const BMaterialImport& material) {
				return material.isPbr;
			})));
}

TEST_CASE("A glTF that will not parse is reported, not guessed at", "[bmesh][gltf]")
{
	const auto path = writeTempGltf("{ not json", "bmesh_broken_test.gltf");

	CHECK_THROWS_AS(probeGltfMaterials(path), std::runtime_error);

	std::filesystem::remove(path);
}
