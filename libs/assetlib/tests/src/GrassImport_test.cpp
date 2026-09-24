#include "PointsGltf.h"
#include <assetlib/asset_import.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/import_document.h>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/Node.h>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

// A glTF POINTS primitive read as a grass field: what it keeps of each point, how it is chunked,
// what it refuses, and how a `.bimport`'s bindings reach it without disturbing the mesh's own.

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	imp::BMeshImport
	Load(const Glb& glb)
	{
		return loadFromGltf(glb.Path(), { .textures = GltfTextures::kSkip });
	}
}

TEST_CASE(
	"A POINTS primitive becomes a grass field, beside the mesh's submeshes",
	"[grass][import]")
{
	Buffer     buffer;
	const auto points = ShuffledGrid(15);  // 225 clumps: three full chunks and a short one
	const Glb  glb("bernini_grass_points.glb", StreetDocument(buffer, points), buffer.bytes);

	const imp::BMeshImport mesh = Load(glb);
	CHECK(mesh.submeshes.size() == 1);

	const BGrassFields& grass = mesh.grass;
	REQUIRE(grass.fields.size() == 1);
	REQUIRE(grass.names == std::vector<std::string>{ "Street[1]" });
	CHECK(grass.fields[0].mesh == 0);
	CHECK(grass.fields[0].look == c_InvalidIndex);
	CHECK(grass.looks.empty());
	CHECK(grass.clumps.size() == points.size());

	REQUIRE(grass.fields[0].chunkCount == 4);
	for (uint32_t c = 0; c < 4; ++c)
	{
		const GrassChunk& chunk = grass.chunks[grass.fields[0].firstChunk + c];
		CHECK(
			chunk.clumpCount == (c < 3 ? c_GrassClumpsPerChunk : 225 - 3 * c_GrassClumpsPerChunk));
		CHECK(chunk.maxHeightScale == 1.0f);

		for (uint32_t k = 0; k < chunk.clumpCount; ++k)
		{
			const GrassClump& clump = grass.clumps[chunk.firstClump + k];
			CHECK(
				glm::distance(clump.position, chunk.boundingCenter) <=
				chunk.boundingRadius + 1e-5f);
			CHECK(clump.normal == glm::vec3(0.0f, 1.0f, 0.0f));
			CHECK(clump.heightScale == 1.0f);
			CHECK(clump.color == glm::u8vec4(255));
		}
	}
}

TEST_CASE(
	"Grass chunks are patches of ground, whatever order the points were exported in",
	"[grass][import]")
{
	Buffer    buffer;
	const Glb glb(
		"bernini_grass_coherent.glb",
		StreetDocument(buffer, ShuffledGrid(64)),
		buffer.bytes);

	const BGrassFields grass = Load(glb).grass;

	// 4096 points one metre apart: a chunk of 64 is an 8 x 8 patch, a sphere of radius ~5 m. Taken
	// in the shuffled order a chunk would span the 64 m field. A quarter of the field is the bound.
	for (const GrassChunk& chunk : grass.chunks) CHECK(chunk.boundingRadius < 16.0f);

	// A re-import produces the same field, byte for byte.
	const BGrassFields again = Load(glb).grass;
	REQUIRE(again.clumps.size() == grass.clumps.size());
	CHECK(
		std::memcmp(
			again.clumps.data(),
			grass.clumps.data(),
			grass.clumps.size() * sizeof(GrassClump)) == 0);
	CHECK(
		std::memcmp(
			again.chunks.data(),
			grass.chunks.data(),
			grass.chunks.size() * sizeof(GrassChunk)) == 0);
}

TEST_CASE("A grass point keeps its normal, colour and height scale", "[grass][import]")
{
	Buffer                       buffer;
	const std::vector<glm::vec3> normals = { glm::vec3(0, 2, 0), glm::vec3(3, 4, 0) };
	const std::vector<uint8_t>   colors  = { 255, 0, 0, 255, 0, 128, 0, 255 };
	const std::vector<float>     heights = { 0.5f, 2.0f };

	auto attributes       = nlohmann::json::object();
	attributes["NORMAL"]  = buffer.Add(normals, "VEC3", c_Float);
	attributes["COLOR_0"] = buffer.Add(colors, "VEC4", c_UnsignedByte, true);
	attributes["_HEIGHT"] = buffer.Add(heights, "SCALAR", c_Float);

	const std::vector<glm::vec3> points = { glm::vec3(0, 0, 0), glm::vec3(1, 0, 0) };
	const Glb                    glb(
		"bernini_grass_attributes.glb",
		StreetDocument(buffer, points, attributes),
		buffer.bytes);

	const BGrassFields grass = Load(glb).grass;
	REQUIRE(grass.clumps.size() == 2);

	// The chunk holds them in Morton order, which along x is source order here.
	const GrassClump& first  = grass.clumps[0];
	const GrassClump& second = grass.clumps[1];
	CHECK(first.normal == glm::vec3(0, 1, 0));
	CHECK(glm::distance(second.normal, glm::vec3(0.6f, 0.8f, 0.0f)) < 1e-6f);
	CHECK(first.color == glm::u8vec4(255, 0, 0, 255));
	CHECK(second.color == glm::u8vec4(0, 128, 0, 255));
	CHECK(first.heightScale == 0.5f);
	CHECK(second.heightScale == 2.0f);
	CHECK(grass.chunks[0].maxHeightScale == 2.0f);
}

TEST_CASE("A POINTS primitive refuses a point no pass could place", "[grass][import]")
{
	const float nan = std::numeric_limits<float>::quiet_NaN();

	SECTION("a non-finite position")
	{
		Buffer    buffer;
		const Glb glb(
			"bernini_grass_nan.glb",
			StreetDocument(buffer, { glm::vec3(nan, 0, 0) }),
			buffer.bytes);
		CHECK_THROWS(Load(glb));
	}

	SECTION("a zero height scale")
	{
		Buffer buffer;
		auto   attributes     = nlohmann::json::object();
		attributes["_HEIGHT"] = buffer.Add(std::vector<float>{ 0.0f }, "SCALAR", c_Float);
		const Glb glb(
			"bernini_grass_flat.glb",
			StreetDocument(buffer, { glm::vec3(0) }, attributes),
			buffer.bytes);
		CHECK_THROWS(Load(glb));
	}

	SECTION("a height that is not a scalar")
	{
		Buffer buffer;
		auto   attributes     = nlohmann::json::object();
		attributes["_HEIGHT"] = buffer.Add(std::vector<glm::vec2>{ glm::vec2(1) }, "VEC2", c_Float);
		const Glb glb(
			"bernini_grass_vec2.glb",
			StreetDocument(buffer, { glm::vec3(0) }, attributes),
			buffer.bytes);
		CHECK_THROWS(Load(glb));
	}

	SECTION("a zero normal")
	{
		Buffer buffer;
		auto   attributes    = nlohmann::json::object();
		attributes["NORMAL"] = buffer.Add(std::vector<glm::vec3>{ glm::vec3(0) }, "VEC3", c_Float);
		const Glb glb(
			"bernini_grass_nonormal.glb",
			StreetDocument(buffer, { glm::vec3(0) }, attributes),
			buffer.bytes);
		CHECK_THROWS(Load(glb));
	}
}

TEST_CASE(
	"A .bimport binds a grass field by name, apart from the mesh's materials",
	"[grass][import]")
{
	Buffer    buffer;
	const Glb glb("bernini_grass_bind.glb", StreetDocument(buffer, ShuffledGrid(4)), buffer.bytes);
	imp::BMeshImport mesh = Load(glb);

	const std::vector<MaterialBinding> bindings = {
		{ .submesh = "Street[0]", .material = "Authored/Materials/road.bmaterial" },
		{ .submesh = "Street[1]", .material = "Authored/Grass/verge.bgrass" },
		{ .submesh = "Gone[3]", .material = "Authored/Grass/old.bgrass" },
	};

	BGrassFields grass = mesh.grass;
	CHECK(applyGrassBindings(grass, bindings) == std::vector<std::string>{ "Gone[3]" });
	CHECK(grass.looks == std::vector<std::string>{ "Authored/Grass/verge.bgrass" });
	CHECK(grass.fields[0].look == 0);

	// Unbound again once the document stops naming it.
	CHECK(applyGrassBindings(grass, std::span(bindings.data(), 1)).empty());
	CHECK(grass.fields[0].look == c_InvalidIndex);
	CHECK(grass.looks.empty());

	CHECK(isGrassBinding(bindings[1]));
	CHECK_FALSE(isGrassBinding(bindings[0]));
}

TEST_CASE("A mesh's bindings neither apply nor report a grass binding", "[grass][import]")
{
	Buffer    buffer;
	const Glb glb(
		"bernini_grass_meshbind.glb",
		StreetDocument(buffer, ShuffledGrid(4)),
		buffer.bytes);
	const imp::BMeshImport import = Load(glb);

	BMesh mesh;
	mesh.submeshes  = import.submeshes;
	mesh.stringPool = import.stringPool;

	const std::vector<MaterialBinding> bindings = {
		{ .submesh = "Street[0]", .material = "Authored/Materials/road.bmaterial" },
		{ .submesh = "Street[1]", .material = "Authored/Grass/verge.bgrass" },
	};
	CHECK(rebuildMaterialSlots(mesh, bindings, {}).empty());
	CHECK(mesh.materials == std::vector<std::string>{ "Authored/Materials/road.bmaterial" });
}

TEST_CASE("A primitive that is neither triangles nor points is still refused", "[grass][import]")
{
	Buffer buffer;
	auto   document = StreetDocument(buffer, { glm::vec3(0), glm::vec3(1) });
	document["meshes"][0]["primitives"][1]["mode"] = 1;  // LINES
	const Glb glb("bernini_grass_lines.glb", document, buffer.bytes);
	CHECK_THROWS(Load(glb));
}

TEST_CASE(
	"Two grass fields of one name are refused where a binding would address them",
	"[grass][import]")
{
	auto grass   = BGrassFields();
	grass.fields = { GrassField{ .mesh = 0, .look = c_InvalidIndex },
		             GrassField{ .mesh = 1, .look = c_InvalidIndex } };
	grass.names  = { "Verge", "Verge" };

	const std::vector<MaterialBinding> bindings = { { .submesh  = "Verge",
		                                              .material = "Authored/Grass/verge.bgrass" } };
	CHECK_THROWS(applyGrassBindings(grass, bindings));
}
