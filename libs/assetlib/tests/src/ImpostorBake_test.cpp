#include "PointsGltf.h"
#include "SkinnedGltf.h"
#include "bmesh/impostor_bake.h"
#include <array>
#include <assetlib/bmesh_gltf.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <vector>

// The impostor bake: what a frame of the atlas shows of a mesh whose every view is known -- a
// square facing +z -- and how a glTF opts a mesh in. The frames are read back as the impostor stage
// would sample them, so a wrong frame direction, a flipped face or a mip that bleeds between views
// fails here rather than as a tree that looks wrong from one side.

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	/** A frame's texel of one mip, in one of the two atlases of `baked`. */
	struct Texel
	{
		uint8_t r, g, b, a;
	};

	Texel
	TexelAt(
		const BakedImpostor& baked,
		const bool           normalDepth,
		const uint32_t       mip,
		const uint32_t       fx,
		const uint32_t       fy,
		const uint32_t       tx,
		const uint32_t       ty)
	{
		size_t   offset = normalDepth ? c_ImpostorAtlasBytes : 0;
		uint32_t side   = c_ImpostorAtlasTexels;
		for (uint32_t m = 0; m < mip; ++m)
		{
			offset += static_cast<size_t>(side) * side * 4;
			side /= 2;
		}
		const uint32_t frame = c_ImpostorFrameTexels >> mip;
		const size_t   i =
			offset + (static_cast<size_t>(fy * frame + ty) * side + fx * frame + tx) * 4;
		return { baked.texels[i], baked.texels[i + 1], baked.texels[i + 2], baked.texels[i + 3] };
	}

	/** Mean coverage over a frame of the first mip. */
	float
	FrameCoverage(const BakedImpostor& baked, const uint32_t fx, const uint32_t fy)
	{
		double sum = 0.0;
		for (uint32_t ty = 0; ty < c_ImpostorFrameTexels; ++ty)
			for (uint32_t tx = 0; tx < c_ImpostorFrameTexels; ++tx)
				sum += TexelAt(baked, false, 0, fx, fy, tx, ty).a;
		return static_cast<float>(sum / (255.0 * c_ImpostorFrameTexels * c_ImpostorFrameTexels));
	}

	/** A 2 x 2 square in the z = 0 plane, wound to face +z, with position, normal and uv. */
	struct Square
	{
		std::vector<std::byte> vertices;
		std::vector<std::byte> indices;
		Submesh                submesh{};

		Square()
		{
			const std::array<std::array<float, 8>, 4> corners = { { { -1, -1, 0, 0, 0, 1, 0, 1 },
				                                                    { 1, -1, 0, 0, 0, 1, 1, 1 },
				                                                    { 1, 1, 0, 0, 0, 1, 1, 0 },
				                                                    { -1, 1, 0, 0, 0, 1, 0, 0 } } };
			vertices.resize(sizeof(corners));
			std::memcpy(vertices.data(), corners.data(), sizeof(corners));
			const std::array<uint16_t, 6> triangles = { 0, 1, 2, 0, 2, 3 };
			indices.resize(sizeof(triangles));
			std::memcpy(indices.data(), triangles.data(), sizeof(triangles));

			submesh.layout.attributes[0]  = { VertexSemantic::kPosition,
				                              VertexFormat::kFloat32x3,
				                              0 };
			submesh.layout.attributes[1]  = { VertexSemantic::kNormal,
				                              VertexFormat::kFloat32x3,
				                              12 };
			submesh.layout.attributes[2]  = { VertexSemantic::kTexCoord0,
				                              VertexFormat::kFloat32x2,
				                              24 };
			submesh.layout.attributeCount = 3;
			submesh.layout.stride         = 32;
			submesh.vertexCount           = 4;
			submesh.indexCount            = 6;
			submesh.indexType             = IndexType::kUint16;
		}

		BakedImpostor
		Bake(const ImpostorSurface& surface) const
		{
			const std::array<ImpostorSurface, 1> surfaces = { surface };
			return bakeImpostor(
				ImpostorSource{ .submeshes  = std::span(&submesh, 1),
			                    .surfaces   = surfaces,
			                    .vertexData = vertices,
			                    .indexData  = indices });
		}
	};

	// Frame (7, 0) is seen from +z, frame (0, 7) from -z, both on the horizon.
	constexpr uint32_t c_FrontX = 7, c_FrontY = 0;
	constexpr uint32_t c_BackX = 0, c_BackY = 7;
}

TEST_CASE(
	"the atlas's corner frames are the horizon's four sides, its middle the view from above",
	"[impostor]")
{
	CHECK(glm::length(impostorFrameDirection(7, 0) - glm::vec3(0, 0, 1)) < 1e-6f);
	CHECK(glm::length(impostorFrameDirection(0, 7) - glm::vec3(0, 0, -1)) < 1e-6f);
	CHECK(glm::length(impostorFrameDirection(0, 0) - glm::vec3(-1, 0, 0)) < 1e-6f);
	CHECK(glm::length(impostorFrameDirection(7, 7) - glm::vec3(1, 0, 0)) < 1e-6f);
	CHECK(impostorFrameDirection(3, 3).y > 0.9f);
	for (uint32_t y = 0; y < c_ImpostorFramesPerSide; ++y)
		for (uint32_t x = 0; x < c_ImpostorFramesPerSide; ++x)
			CHECK(impostorFrameDirection(x, y).y >= -1e-6f);
}

TEST_CASE("a frame shows the mesh's silhouette, colour and normal from its direction", "[impostor]")
{
	const Square        square;
	const BakedImpostor baked =
		square.Bake({ .baseColorFactor = glm::vec4(0.8f, 0.2f, 0.1f, 1.0f) });

	CHECK(baked.record.center == glm::vec3(0.0f));
	CHECK(baked.record.radius == Catch::Approx(std::sqrt(2.0f)));
	REQUIRE(baked.texels.size() == 2 * size_t{ c_ImpostorAtlasBytes });

	// Seen face on, the square spans 1 / sqrt(2) of the sphere's diameter each way: half the frame.
	CHECK(FrameCoverage(baked, c_FrontX, c_FrontY) == Catch::Approx(0.5f).margin(0.02f));

	const Texel middle = TexelAt(baked, false, 0, c_FrontX, c_FrontY, 64, 64);
	CHECK(middle.a == 255);
	CHECK(middle.r == 204);  // linear, as the factor is
	CHECK(middle.g == 51);

	const Texel normal = TexelAt(baked, true, 0, c_FrontX, c_FrontY, 64, 64);
	CHECK(normal.b == 255);  // +z
	CHECK(normal.r == 128);
	CHECK(static_cast<int>(normal.a) == 128);  // the square passes through the sphere's centre

	// From above the square is edge on.
	CHECK(FrameCoverage(baked, 3, 3) < 0.05f);
}

TEST_CASE(
	"a single-sided face is not baked from behind, a double-sided one faces the viewer",
	"[impostor]")
{
	const Square square;

	CHECK(FrameCoverage(square.Bake({}), c_BackX, c_BackY) == 0.0f);

	const BakedImpostor both = square.Bake({ .doubleSided = true });
	CHECK(FrameCoverage(both, c_BackX, c_BackY) == Catch::Approx(0.5f).margin(0.02f));
	CHECK(TexelAt(both, true, 0, c_BackX, c_BackY, 64, 64).b == 0);  // -z, toward that viewer
}

TEST_CASE("a frame's mips never take another frame's view", "[impostor]")
{
	const BakedImpostor baked = Square().Bake({});

	// The back frame saw nothing, and its neighbour on the grid saw the square from nearly behind.
	for (uint32_t mip = 1; mip < c_ImpostorAtlasMips; ++mip)
	{
		const uint32_t frame = c_ImpostorFrameTexels >> mip;
		for (uint32_t ty = 0; ty < frame; ++ty)
			for (uint32_t tx = 0; tx < frame; ++tx)
				CHECK(TexelAt(baked, false, mip, c_BackX, c_BackY, tx, ty).a == 0);
	}
	CHECK(TexelAt(baked, false, c_ImpostorAtlasMips - 1, c_FrontX, c_FrontY, 4, 4).a == 255);
}

TEST_CASE(
	"a base colour texture is sampled in linear colour, and a masked texel is cut",
	"[impostor]")
{
	const Square square;

	// sRGB 188 is linear 0.5.
	const std::array<uint8_t, 4> grey   = { 188, 188, 188, 255 };
	const ImpostorImage          opaque = { 1, 1, grey };
	const Texel                  sampled =
		TexelAt(square.Bake({ .baseColor = &opaque }), false, 0, c_FrontX, c_FrontY, 64, 64);
	CHECK(sampled.r == Catch::Approx(128).margin(1));

	const std::array<uint8_t, 4> clear  = { 255, 255, 255, 10 };
	const ImpostorImage          cutout = { 1, 1, clear };
	CHECK(
		FrameCoverage(
			square.Bake({ .baseColor = &cutout, .alphaTest = true }),
			c_FrontX,
			c_FrontY) == 0.0f);
}

TEST_CASE("a bake with nothing to draw is refused", "[impostor]")
{
	Square square;
	square.submesh.indexCount = 0;
	CHECK_THROWS_AS(square.Bake({}), std::runtime_error);
}

namespace
{
	/** A glb of one square, its mesh's and node's extras as given. */
	Glb
	SquareGlb(
		const char*                                 file,
		const nlohmann::json&                       meshExtras,
		const nlohmann::json&                       nodeExtras,
		const std::function<void(nlohmann::json&)>& edit = {})
	{
		auto                         buffer    = Buffer();
		const std::vector<glm::vec3> positions = { { -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 },
			                                       { -1, -1, 0 }, { 1, 1, 0 },  { -1, 1, 0 } };
		const std::vector<glm::vec3> normals(6, glm::vec3(0, 0, 1));
		auto primitive = nlohmann::json{ { "attributes",
			                               { { "POSITION", buffer.Add(positions, "VEC3", c_Float) },
			                                 { "NORMAL", buffer.Add(normals, "VEC3", c_Float) } } },
			                             { "material", 0 } };
		auto mesh      = nlohmann::json{ { "name", "Square" }, { "primitives", { primitive } } };
		if (!meshExtras.is_null())
			mesh["extras"] = meshExtras;
		auto node = nlohmann::json{ { "mesh", 0 }, { "name", "Square" } };
		if (!nodeExtras.is_null())
			node["extras"] = nodeExtras;

		auto document           = nlohmann::json::object();
		document["scene"]       = 0;
		document["scenes"]      = { { { "nodes", { 0 } } } };
		document["nodes"]       = { node };
		document["meshes"]      = { mesh };
		document["materials"]   = { { { "name", "Leaf" },
			                          { "pbrMetallicRoughness",
			                            { { "baseColorFactor", { 0.25, 0.5, 0.125, 1.0 } } } } } };
		document["bufferViews"] = buffer.views;
		document["accessors"]   = buffer.accessors;
		if (edit)
			edit(document);
		return Glb(file, document, buffer);
	}
}

TEST_CASE(
	"a mesh opts into an impostor through its extras or its node's, as Blender exports them",
	"[impostor][gltf]")
{
	SECTION("the mesh's own Custom Property")
	{
		const Glb glb =
			SquareGlb("bernini_impostor_mesh.glb", { { "bernini_impostor", true } }, nullptr);
		const imp::BMeshImport mesh = loadFromGltf(glb.Path(), { .textures = GltfTextures::kSkip });
		REQUIRE(mesh.impostors.size() == 1);
		CHECK(mesh.impostors[0].mesh == 0);
		CHECK(mesh.impostorTexels.size() == 2 * size_t{ c_ImpostorAtlasBytes });

		// The glTF's own base colour, with no material import: regeneration runs without one.
		CHECK(
			mesh.impostorTexels
				[(c_FrontY * c_ImpostorFrameTexels + 64) * 4 * c_ImpostorAtlasTexels +
		         (c_FrontX * c_ImpostorFrameTexels + 64) * 4 + 1] == 128);
	}

	SECTION("the object's, which Blender writes on the node")
	{
		const Glb glb =
			SquareGlb("bernini_impostor_node.glb", nullptr, { { "bernini_impostor", 1 } });
		CHECK(loadFromGltf(glb.Path()).impostors.size() == 1);
	}

	SECTION("none, or turned off")
	{
		const Glb none = SquareGlb("bernini_impostor_none.glb", nullptr, nullptr);
		CHECK(loadFromGltf(none.Path()).impostors.empty());
		const Glb off =
			SquareGlb("bernini_impostor_off.glb", { { "bernini_impostor", false } }, nullptr);
		CHECK(loadFromGltf(off.Path()).impostors.empty());
	}

	SECTION("a blended material is cut at half coverage, as a leaf card is drawn")
	{
		const auto clear = [](nlohmann::json& document) {
			document["materials"][0]["alphaMode"]                               = "BLEND";
			document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = { 0.25,
				                                                                    0.5,
				                                                                    0.125,
				                                                                    0.2 };
		};
		const Glb glb = SquareGlb(
			"bernini_impostor_blend.glb",
			{ { "bernini_impostor", true } },
			nullptr,
			clear);
		const imp::BMeshImport mesh = loadFromGltf(glb.Path());
		REQUIRE(mesh.impostors.size() == 1);
		CHECK(
			mesh.impostorTexels
				[(c_FrontY * c_ImpostorFrameTexels + 64) * 4 * c_ImpostorAtlasTexels +
		         (c_FrontX * c_ImpostorFrameTexels + 64) * 4 + 3] == 0);
	}

	SECTION("a value that is not a truth is dropped, and the mesh still imports")
	{
		const Glb glb =
			SquareGlb("bernini_impostor_string.glb", { { "bernini_impostor", "yes" } }, nullptr);
		const imp::BMeshImport mesh = loadFromGltf(glb.Path());
		CHECK(mesh.impostors.empty());
		CHECK(mesh.meshes.size() == 1);
	}
}

TEST_CASE("a skinned mesh that asks for an impostor imports without one", "[impostor][gltf]")
{
	const SkinnedGltf rig(
		"bernini_impostor_skinned",
		{ { R"("meshes": [ { "name": "body",)",
	        R"("meshes": [ { "name": "body", "extras": { "bernini_impostor": true },)" } });
	const imp::BMeshImport mesh = loadFromGltf(rig.gltf);
	CHECK(mesh.impostors.empty());
	CHECK_FALSE(mesh.skeleton.bones.empty());
}
