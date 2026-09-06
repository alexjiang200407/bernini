#include <algorithm>
#include <array>
#include <assetlib/bmesh.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/codecs.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMaterialImport.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/VertexLayout.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>

#include <catch2/catch_approx.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <vector>

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
  "extensionsUsed": [ "KHR_materials_unlit", "KHR_materials_pbrSpecularGlossiness", "KHR_materials_transmission", "KHR_materials_specular" ],
  "materials": [
    { "name": "plain", "pbrMetallicRoughness": { "metallicFactor": 0.25, "roughnessFactor": 0.5 } },
    { "name": "leaves", "alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true },
    { "name": "glass", "alphaMode": "BLEND" },
    { "name": "sign", "extensions": { "KHR_materials_unlit": {} } },
    { "name": "old", "extensions": { "KHR_materials_pbrSpecularGlossiness": {} } },
    { "name": "lens", "alphaMode": "BLEND", "extensions": { "KHR_materials_transmission": { "transmissionFactor": 0.85 } } },
    { "name": "fur", "extensions": { "KHR_materials_specular": { "specularFactor": 0.0 } } },
    { "name": "gilded", "extensions": { "KHR_materials_specular": { "specularColorFactor": [ 1.0, 0.77, 0.34 ] } } },
    { "name": "clamped", "extensions": { "KHR_materials_specular": { "specularFactor": 3.5, "specularColorFactor": [ -1.0, 0.5, 0.5 ] } } },
    { "name": "empty", "extensions": { "KHR_materials_specular": {} } }
  ]
})";

	/**
	 * Specular-glossiness materials with values chosen so the conversion lands on exact numbers: a
	 * black specular is unambiguously dielectric, a white one against a black diffuse is
	 * unambiguously metal, and each has a glossiness whose complement is exact in binary.
	 *
	 * `vinyl` sits between the two, at half the dielectric F0 -- the band the metallic-roughness
	 * half cannot express at all, since it solves to the same metallic of 0 as `clay`.
	 */
	constexpr const char* c_SpecGlossGltf = R"({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ "KHR_materials_pbrSpecularGlossiness" ],
  "extensionsRequired": [ "KHR_materials_pbrSpecularGlossiness" ],
  "materials": [
    { "name": "clay", "extensions": { "KHR_materials_pbrSpecularGlossiness": {
        "diffuseFactor": [ 0.5, 0.4, 0.3, 1.0 ], "specularFactor": [ 0.0, 0.0, 0.0 ],
        "glossinessFactor": 0.25 } } },
    { "name": "chrome", "extensions": { "KHR_materials_pbrSpecularGlossiness": {
        "diffuseFactor": [ 0.0, 0.0, 0.0, 1.0 ], "specularFactor": [ 1.0, 1.0, 1.0 ],
        "glossinessFactor": 1.0 } } },
    { "name": "bare", "extensions": { "KHR_materials_pbrSpecularGlossiness": {} } },
    { "name": "vinyl", "extensions": { "KHR_materials_pbrSpecularGlossiness": {
        "diffuseFactor": [ 0.5, 0.5, 0.5, 1.0 ], "specularFactor": [ 0.02, 0.02, 0.02 ],
        "glossinessFactor": 0.5 } } }
  ]
})";

	std::filesystem::path
	WriteTempGltf(const char* json = c_TriangleGltf, const char* name = "bmesh_triangle_test.gltf")
	{
		const auto    path = std::filesystem::temp_directory_path() / name;
		std::ofstream out(path, std::ios::binary);
		out << json;
		return path;
	}

	/** The materials document, imported and cleaned up. */
	BMeshImport
	LoadMaterialsGltf()
	{
		const auto path = WriteTempGltf(c_MaterialsGltf, "bmesh_materials_test.gltf");
		auto       mesh = loadFromGltf(path);
		std::filesystem::remove(path);
		return mesh;
	}
}

TEST_CASE("loadFromGltf imports geometry and hierarchy from a triangle", "[bmesh][gltf]")
{
	// A name of its own: the suite is sharded across concurrent processes, so two cases sharing a
	// temp file race to delete it out from under each other.
	const auto path = WriteTempGltf(c_TriangleGltf, "bmesh_triangle_geometry_test.gltf");
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
	const auto path = WriteTempGltf(c_TriangleGltf, "bmesh_triangle_roundtrip_test.gltf");
	const auto mesh = loadFromGltf(path);
	std::filesystem::remove(path);

	const auto restored =
		AssetCodec<BMesh>::Deserialize(AssetCodec<BMesh>::Serialize(toBMesh(mesh)));
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
	const auto restored =
		AssetCodec<BMesh>::Deserialize(AssetCodec<BMesh>::Serialize(toBMesh(mesh)));
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
	const auto mesh = LoadMaterialsGltf();
	REQUIRE(mesh.materials.size() == 10);

	// Each of glTF's three alpha modes maps to its own: OPAQUE, MASK (alpha test), BLEND (alpha blend).
	CHECK(mesh.materials[0].alphaMode == AlphaMode::kOpaque);
	CHECK(mesh.materials[1].alphaMode == AlphaMode::kMask);
	CHECK(mesh.materials[1].alphaCutoff == Catch::Approx(0.3f));
	CHECK(mesh.materials[2].alphaMode == AlphaMode::kBlend);

	// glTF's own default, not the engine's: a MASK material that names no cutoff cuts at 0.5.
	CHECK(mesh.materials[0].alphaCutoff == Catch::Approx(0.5f));

	// doubleSided comes across as the file says it, and glTF's default is one side.
	CHECK(mesh.materials[1].doubleSided);
	CHECK(!mesh.materials[0].doubleSided);
}

// A lens and a hair card both export as BLEND, and the alpha means something different in each: how
// much light the surface passes, against how much of the pixel it covers. KHR_materials_transmission
// is the only thing in the file that tells them apart, and without it the engine reads every blended
// material as coverage -- which costs the lens its reflection.
TEST_CASE("A glTF's transmission factor comes across", "[bmesh][gltf]")
{
	const auto mesh = LoadMaterialsGltf();
	REQUIRE(mesh.materials.size() == 10);

	CHECK(mesh.materials[5].alphaMode == AlphaMode::kBlend);
	CHECK(mesh.materials[5].transmissionFactor == Catch::Approx(0.85f));

	// glTF's own default, and the reading BLEND has always had here: a blended material that does
	// not declare the extension is coverage, not glass.
	CHECK(mesh.materials[2].alphaMode == AlphaMode::kBlend);
	CHECK(mesh.materials[2].transmissionFactor == 0.0f);
}

// The only thing in glTF that can say a surface has *no* specular. A Phong export with its specular
// switched off carries that intent nowhere else, and without the extension every such material
// arrives at the flat 0.04 dielectric and wears a sheen its author removed.
TEST_CASE("A glTF's specular factors come across", "[bmesh][gltf]")
{
	const auto mesh = LoadMaterialsGltf();
	REQUIRE(mesh.materials.size() == 10);

	CHECK(mesh.materials[6].specularFactor == 0.0f);
	CHECK(mesh.materials[6].specularColorFactor == glm::vec3(1.0f));

	CHECK(mesh.materials[7].specularFactor == Catch::Approx(1.0f));
	CHECK(mesh.materials[7].specularColorFactor.r == Catch::Approx(1.0f));
	CHECK(mesh.materials[7].specularColorFactor.g == Catch::Approx(0.77f));
	CHECK(mesh.materials[7].specularColorFactor.b == Catch::Approx(0.34f));

	// Out-of-range values clamp rather than reaching the shader: a factor above 1 would brighten the
	// lobe past the dielectric it scales, and a negative tint is not a colour.
	CHECK(mesh.materials[8].specularFactor == Catch::Approx(1.0f));
	CHECK(mesh.materials[8].specularColorFactor.r == 0.0f);

	// A declared but empty extension is every default, which is also what no extension at all means.
	CHECK(mesh.materials[9].specularFactor == Catch::Approx(1.0f));
	CHECK(mesh.materials[9].specularColorFactor == glm::vec3(1.0f));
	CHECK(mesh.materials[0].specularFactor == Catch::Approx(1.0f));
	CHECK(mesh.materials[0].specularColorFactor == glm::vec3(1.0f));
}

TEST_CASE("A material declaring another shading model is not PBR", "[bmesh][gltf]")
{
	const auto mesh = LoadMaterialsGltf();
	REQUIRE(mesh.materials.size() == 10);

	// Metallic-roughness is glTF's shading model, so a material is PBR unless it says otherwise. Unlit
	// is the only thing that does: it names a shading model the engine does not have, and its fields
	// are glTF's defaults rather than the author's intent, so importing it as PBR would be a lie.
	// Specular-glossiness is converted instead, which is an approximation and a documented one.
	CHECK(mesh.materials[0].isPbr);
	CHECK(mesh.materials[1].isPbr);
	CHECK(mesh.materials[2].isPbr);
	CHECK_FALSE(mesh.materials[3].isPbr);  // KHR_materials_unlit
	CHECK(mesh.materials[4].isPbr);        // KHR_materials_pbrSpecularGlossiness, converted
}

TEST_CASE("A specular-glossiness material converts to metallic-roughness", "[bmesh][gltf]")
{
	const auto path = WriteTempGltf(c_SpecGlossGltf, "bmesh_specgloss_test.gltf");
	const auto mesh = loadFromGltf(path);
	std::filesystem::remove(path);

	REQUIRE(mesh.materials.size() == 4);

	// A black specular cannot be metal, so the diffuse survives as base colour -- divided by the
	// 0.96 a dielectric does not reflect, which is what makes the two models agree on the lobe.
	const auto& clay = mesh.materials[0];
	CHECK(clay.isPbr);
	CHECK(clay.metallicFactor == Catch::Approx(0.0f));
	CHECK(clay.roughnessFactor == Catch::Approx(0.75f));
	CHECK(clay.baseColorFactor.r == Catch::Approx(0.5f / 0.96f));
	CHECK(clay.baseColorFactor.g == Catch::Approx(0.4f / 0.96f));
	CHECK(clay.baseColorFactor.b == Catch::Approx(0.3f / 0.96f));
	CHECK(clay.baseColorFactor.a == Catch::Approx(1.0f));

	// The switched-off Phong specular, which metallic-roughness alone cannot say: metallic is 0 for
	// every non-metal alike, so only the specular pair separates this from an ordinary dielectric.
	// The scalar has to go with the colour, or the split-sum's F90 term keeps a grazing rim.
	CHECK(clay.specularColorFactor == glm::vec3(0.0f));
	CHECK(clay.specularFactor == 0.0f);

	// A white specular over a black diffuse is the one case that solves to a full metal, and its
	// base colour is the specular it reflects rather than the diffuse it does not have.
	const auto& chrome = mesh.materials[1];
	CHECK(chrome.metallicFactor == Catch::Approx(1.0f));
	CHECK(chrome.roughnessFactor == Catch::Approx(0.0f));
	CHECK(chrome.baseColorFactor.r == Catch::Approx(1.0f));

	// Above the dielectric line the reflection is already in metallic and the base colour, so the
	// pair stays at the defaults every material had before the conversion wrote it.
	CHECK(chrome.specularColorFactor == glm::vec3(1.0f));
	CHECK(chrome.specularFactor == 1.0f);

	// An empty extension is glTF's defaults -- white diffuse, white specular, full glossiness -- and
	// not this build's, which is the trap: tinygltf default-constructs pbrMetallicRoughness too, so
	// a material that reached the metallic-roughness path would arrive rough and fully metallic.
	const auto& bare = mesh.materials[2];
	CHECK(bare.isPbr);
	CHECK(bare.roughnessFactor == Catch::Approx(0.0f));
	CHECK(bare.specularColorFactor == glm::vec3(1.0f));
	CHECK(bare.specularFactor == 1.0f);

	// Half the dielectric F0, which solves to the same metallic of 0 as clay: the pair is the only
	// thing that tells the two apart, and the shader rebuilds 0.02 as 0.04 * 0.5.
	const auto& vinyl = mesh.materials[3];
	CHECK(vinyl.metallicFactor == Catch::Approx(0.0f));
	CHECK(vinyl.roughnessFactor == Catch::Approx(0.5f));
	CHECK(vinyl.specularColorFactor.r == Catch::Approx(0.5f));
	CHECK(vinyl.specularColorFactor.g == Catch::Approx(0.5f));
	CHECK(vinyl.specularColorFactor.b == Catch::Approx(0.5f));

	// A dim specular is still an interface, so the lobe stays whole: only an exactly black one
	// means there is none.
	CHECK(vinyl.specularFactor == 1.0f);
}

TEST_CASE("probeGltfMaterials reports the PBR materials", "[bmesh][gltf]")
{
	const auto path   = WriteTempGltf(c_MaterialsGltf, "bmesh_probe_test.gltf");
	const auto probed = probeGltfMaterials(path);
	std::filesystem::remove(path);

	CHECK(probed.size() == 10);

	// KHR_materials_specular layers on metallic-roughness rather than replacing it, and
	// specular-glossiness is converted to it, so unlit is the only material here that is not PBR.
	CHECK(std::ranges::count_if(probed, &GltfMaterial::isPbr) == 9);
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
	const auto probed = probeGltfMaterials(glb);

	REQUIRE_FALSE(import.textures.empty());
	REQUIRE(probed.size() == import.materials.size());

	// Index for index, not merely in bulk: a caller names the file each material will be written to
	// before the import runs, so an entry that lines up with a different material names the wrong file.
	for (size_t i = 0; i < probed.size(); ++i)
	{
		INFO("material " << i);
		CHECK(probed[i].isPbr == import.materials[i].isPbr);
		CHECK(probed[i].name == import.stringPool.at(import.materials[i].nameOffset));
	}
}

TEST_CASE("A glTF that will not parse is reported, not guessed at", "[bmesh][gltf]")
{
	const auto path = WriteTempGltf("{ not json", "bmesh_broken_test.gltf");

	CHECK_THROWS_AS(probeGltfMaterials(path), std::runtime_error);

	std::filesystem::remove(path);
}

namespace
{
	// Three 1x1 PNGs inline, so the occlusion cases run without a fixture on disk: buildTextures skips
	// an image it cannot decode, and an index into `textures` only exists for one that decoded.
	constexpr const char* c_OcclusionGltf = R"({
  "asset": { "version": "2.0" },
  "images": [
    { "name": "albedo", "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4//8/AAX+Av4N70a4AAAAAElFTkSuQmCC" },
    { "name": "ao", "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGNwYGAAAADEAEG9pK30AAAAAElFTkSuQmCC" },
    { "name": "mr", "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGNgaPgPAAIDAYAkYfWXAAAAAElFTkSuQmCC" }
  ],
  "textures": [ { "source": 0 }, { "source": 1 }, { "source": 2 } ],
  "materials": [
    { "name": "occlusionOnly", "occlusionTexture": { "index": 1 } },
    { "name": "sharedOrm", "occlusionTexture": { "index": 2 },
      "pbrMetallicRoughness": { "metallicRoughnessTexture": { "index": 2 } } },
    { "name": "separate", "occlusionTexture": { "index": 1 },
      "pbrMetallicRoughness": { "metallicRoughnessTexture": { "index": 2 } } },
    { "name": "secondUvSet", "occlusionTexture": { "index": 1, "texCoord": 1 } },
    { "name": "attenuated", "occlusionTexture": { "index": 1, "strength": 0.5 } },
    { "name": "none", "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } },
    { "name": "specGloss", "occlusionTexture": { "index": 1 },
      "extensions": { "KHR_materials_pbrSpecularGlossiness": { "glossinessFactor": 0.5 } } }
  ]
})";

	BMeshImport
	LoadOcclusionGltf()
	{
		const auto path = WriteTempGltf(c_OcclusionGltf, "bmesh_occlusion_test.gltf");
		auto       mesh = loadFromGltf(path);
		std::filesystem::remove(path);
		return mesh;
	}
}

TEST_CASE("A material's own occlusion map is imported", "[bmesh][gltf][occlusion]")
{
	const BMeshImport mesh = LoadOcclusionGltf();
	REQUIRE(mesh.materials.size() == 7);
	REQUIRE(mesh.textures.size() == 3);

	// The whole defect: an occlusionTexture with no metallic-roughness texture beside it used to
	// arrive as nothing at all, which is every affected asset in the character pack.
	const BMaterialImport& only = mesh.materials[0];
	CHECK(only.occlusionTexture != c_InvalidIndex);
	CHECK(only.ormTexture == c_InvalidIndex);
}

TEST_CASE("A shared-ORM material still names one texture for both", "[bmesh][gltf][occlusion]")
{
	// Reading occlusion must not disturb the convention it was assumed under: the two fields agree,
	// so the routing that follows is identical to what it was before this was read at all.
	const BMeshImport      mesh   = LoadOcclusionGltf();
	const BMaterialImport& shared = mesh.materials[1];

	CHECK(shared.occlusionTexture != c_InvalidIndex);
	CHECK(shared.occlusionTexture == shared.ormTexture);
}

TEST_CASE("Occlusion and metallic-roughness are kept apart", "[bmesh][gltf][occlusion]")
{
	const BMeshImport      mesh     = LoadOcclusionGltf();
	const BMaterialImport& separate = mesh.materials[2];

	REQUIRE(separate.occlusionTexture != c_InvalidIndex);
	REQUIRE(separate.ormTexture != c_InvalidIndex);
	CHECK(separate.occlusionTexture != separate.ormTexture);
}

TEST_CASE(
	"An occlusion map on a second UV set is refused, not resampled",
	"[bmesh][gltf][occlusion]")
{
	// The one outcome worse than dropping it: only TEXCOORD_0 is read, so honouring this index would
	// sample a map baked against another parameterisation and produce confident garbage.
	CHECK(LoadOcclusionGltf().materials[3].occlusionTexture == c_InvalidIndex);
}

TEST_CASE(
	"An occlusion strength the engine cannot apply does not lose the map",
	"[bmesh][gltf][occlusion]")
{
	// strength is a documented gap, not a reason to drop authored data: the map is routed unattenuated
	// and the shortfall is a warning.
	CHECK(LoadOcclusionGltf().materials[4].occlusionTexture != c_InvalidIndex);
}

TEST_CASE("A material with no occlusion map claims none", "[bmesh][gltf][occlusion]")
{
	// AO defaults to white, which is correct; inferring one from the metallic-roughness texture that
	// is not there is what this must not start doing.
	CHECK(LoadOcclusionGltf().materials[5].occlusionTexture == c_InvalidIndex);
}

TEST_CASE("A specular-glossiness material keeps its occlusion map", "[bmesh][gltf][occlusion]")
{
	// The conversion overwrites the metallic-roughness block wholesale, and occlusionTexture is a
	// sibling of it rather than a member -- so a read placed inside that block is silently undone.
	const BMeshImport      mesh      = LoadOcclusionGltf();
	const BMaterialImport& converted = mesh.materials[6];

	REQUIRE(converted.isPbr);
	CHECK(converted.occlusionTexture != c_InvalidIndex);
}

namespace
{
	// Two 2x2 PNGs inline. "gloss" carries a varying alpha, which is where specular-glossiness puts
	// glossiness; "flat" has no alpha channel at all, which stb pads to a constant 255 -- the case
	// that must not be mistaken for a glossiness of 1 everywhere.
	constexpr const char* c_GlossGltf = R"({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ "KHR_materials_pbrSpecularGlossiness" ],
  "extensionsRequired": [ "KHR_materials_pbrSpecularGlossiness" ],
  "images": [
    { "name": "gloss", "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAGklEQVR4nGPkEpFjYGBg+M/EwMDQwMDA4AgAEJkCALS/iFUAAAAASUVORK5CYII=" },
    { "name": "flat", "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAFklEQVR4nGPkEpGTk5NjsbGxkZOTAwAKVgGqAUplOAAAAABJRU5ErkJggg==" }
  ],
  "textures": [ { "source": 0 }, { "source": 1 } ],
  "materials": [
    { "name": "varying", "extensions": { "KHR_materials_pbrSpecularGlossiness": {
        "glossinessFactor": 1.0, "specularGlossinessTexture": { "index": 0 } } } },
    { "name": "sameAgain", "extensions": { "KHR_materials_pbrSpecularGlossiness": {
        "glossinessFactor": 1.0, "specularGlossinessTexture": { "index": 0 } } } },
    { "name": "halfGloss", "extensions": { "KHR_materials_pbrSpecularGlossiness": {
        "glossinessFactor": 0.5, "specularGlossinessTexture": { "index": 0 } } } },
    { "name": "flatAlpha", "extensions": { "KHR_materials_pbrSpecularGlossiness": {
        "glossinessFactor": 0.25, "specularGlossinessTexture": { "index": 1 } } } },
    { "name": "secondUvSet", "extensions": { "KHR_materials_pbrSpecularGlossiness": {
        "glossinessFactor": 1.0,
        "specularGlossinessTexture": { "index": 0, "texCoord": 1 } } } }
  ]
})";

	BMeshImport
	LoadGlossGltf()
	{
		const auto path = WriteTempGltf(c_GlossGltf, "bmesh_gloss_test.gltf");
		auto       mesh = loadFromGltf(path);
		std::filesystem::remove(path);
		return mesh;
	}

	/** The top mip of `texture` as RGBA8 texels, in row order. */
	std::vector<std::array<uint8_t, 4>>
	TopMipTexels(const ImageData& image)
	{
		const ImageSubresource& sub = image.subresources.front();

		auto out = std::vector<std::array<uint8_t, 4>>();
		for (uint32_t y = 0; y < image.height; ++y)
			for (uint32_t x = 0; x < image.width; ++x)
			{
				const std::byte* px = image.pixels.data() + sub.offset + y * sub.rowPitch + x * 4u;
				// Both pairs of braces: std::array wraps a C array, and MSVC's C5246 is an error here.
				out.push_back(
					{ { std::to_integer<uint8_t>(px[0]),
				        std::to_integer<uint8_t>(px[1]),
				        std::to_integer<uint8_t>(px[2]),
				        std::to_integer<uint8_t>(px[3]) } });
			}
		return out;
	}
}

TEST_CASE("A glossiness map becomes a roughness map at import", "[bmesh][gltf][specgloss]")
{
	// The whole defect: glossiness is the complement of roughness, a route selects a channel and
	// cannot transform it, so the complement had to be written once here or not at all.
	const BMeshImport      mesh    = LoadGlossGltf();
	const BMaterialImport& varying = mesh.materials[0];

	REQUIRE(varying.ormTexture != c_InvalidIndex);

	// The factor rides in the texels, so it must not also ride on the material: the shader reads
	// orm.g * roughnessFactor, and a factor in both places is applied twice.
	CHECK(varying.roughnessFactor == 1.0f);

	const std::vector<std::array<uint8_t, 4>> texels =
		TopMipTexels(mesh.textures[varying.ormTexture]);
	REQUIRE(texels.size() == 4);

	// Green is 255 - alpha at a glossiness factor of 1. Red and blue are white, the identity for
	// the occlusion and metallic the shader multiplies them into.
	const std::array<uint8_t, 4> expected = { { 255, 0, 127, 191 } };
	for (size_t i = 0; i < texels.size(); ++i)
	{
		CHECK(texels[i][1] == expected[i]);
		CHECK(texels[i][0] == 255);
		CHECK(texels[i][2] == 255);
	}
}

TEST_CASE("The glossiness factor is folded into the map", "[bmesh][gltf][specgloss]")
{
	const BMeshImport      mesh = LoadGlossGltf();
	const BMaterialImport& half = mesh.materials[2];

	REQUIRE(half.ormTexture != c_InvalidIndex);
	CHECK(half.roughnessFactor == 1.0f);

	// 255 - alpha/2: glTF multiplies glossiness by its factor, and 1 - g*f is not (1 - g)*(1 - f),
	// so the two cannot be split across the map and the material.
	const std::vector<std::array<uint8_t, 4>> texels = TopMipTexels(mesh.textures[half.ormTexture]);
	const std::array<uint8_t, 4>              expected = { { 255, 128, 191, 223 } };
	for (size_t i = 0; i < texels.size(); ++i) CHECK(texels[i][1] == expected[i]);
}

TEST_CASE("One map serves every material that implies the same one", "[bmesh][gltf][specgloss]")
{
	const BMeshImport mesh = LoadGlossGltf();

	// Two decoded images plus two maps: one for the materials at a factor of 1, one for the factor
	// of 0.5. A map per material would be three, and the factor left out of the key would be one.
	CHECK(mesh.textures.size() == 4);
	CHECK(mesh.textures.size() == mesh.textureNames.size());

	CHECK(mesh.materials[0].ormTexture == mesh.materials[1].ormTexture);
	CHECK(mesh.materials[0].ormTexture != mesh.materials[2].ormTexture);
}

TEST_CASE("A glossiness map with nothing in it is refused", "[bmesh][gltf][specgloss]")
{
	// tinygltf pads every image to four channels, so a source with no alpha arrives indistinguishable
	// from one whose alpha is uniform -- and reading either as glossiness would claim a mirror finish
	// the artist never authored. The constant the factor already carried is the honest answer.
	const BMeshImport      mesh = LoadGlossGltf();
	const BMaterialImport& flat = mesh.materials[3];

	CHECK(flat.ormTexture == c_InvalidIndex);
	CHECK(flat.roughnessFactor == Catch::Approx(0.75f));
}

TEST_CASE("A glossiness map on a second UV set is refused", "[bmesh][gltf][specgloss]")
{
	// readOcclusion's rule, for readOcclusion's reason: TEXCOORD_0 is the only set read.
	const BMeshImport      mesh   = LoadGlossGltf();
	const BMaterialImport& second = mesh.materials[4];

	CHECK(second.ormTexture == c_InvalidIndex);
	CHECK(second.roughnessFactor == 0.0f);
}
