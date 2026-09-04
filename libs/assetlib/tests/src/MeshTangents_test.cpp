#include <assetlib/mesh_tangents.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>

#include <assetlib_structs/BMesh.h>

#include <catch2/catch_approx.hpp>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <stdexcept>
#include <vector>

using namespace assetlib;

namespace
{
	struct Vertex
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
	};

	static_assert(sizeof(Vertex) == 32, "the layout below describes this struct");

	/**
	 * One triangle in the XY plane, its UVs running with +X and +Y, so the tangent it implies is
	 * exactly +X and its bitangent exactly +Y -- a case whose answer can be written down.
	 */
	BMesh
	OneTriangle(bool withTangent = false)
	{
		const Vertex verts[3] = {
			{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
			{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
			{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
		};

		auto mesh = BMesh();

		auto submesh                 = Submesh();
		submesh.layout.attributes[0] = { VertexSemantic::kPosition, VertexFormat::kFloat32x3, 0 };
		submesh.layout.attributes[1] = { VertexSemantic::kNormal, VertexFormat::kFloat32x3, 12 };
		submesh.layout.attributes[2] = { VertexSemantic::kTexCoord0, VertexFormat::kFloat32x2, 24 };
		submesh.layout.attributeCount = 3;
		submesh.layout.stride         = sizeof(Vertex);

		if (withTangent)
		{
			submesh.layout.attributes[3]  = { VertexSemantic::kTangent,
				                              VertexFormat::kFloat32x4,
				                              32 };
			submesh.layout.attributeCount = 4;
			submesh.layout.stride         = sizeof(Vertex) + 16;
		}

		submesh.vertexByteOffset = 0;
		submesh.vertexCount      = 3;
		submesh.indexByteOffset  = 0;
		submesh.indexCount       = 3;
		submesh.indexType        = IndexType::kUint32;

		mesh.vertexData.resize(static_cast<size_t>(submesh.layout.stride) * 3, std::byte{ 0 });
		for (int v = 0; v < 3; ++v)
		{
			std::memcpy(
				mesh.vertexData.data() + static_cast<size_t>(v) * submesh.layout.stride,
				&verts[v],
				sizeof(Vertex));
		}

		const uint32_t indices[3] = { 0, 1, 2 };
		mesh.indexData.resize(sizeof(indices));
		std::memcpy(mesh.indexData.data(), indices, sizeof(indices));

		mesh.submeshes.push_back(submesh);
		return mesh;
	}

	glm::vec4
	TangentOf(const BMesh& mesh, size_t submeshIndex, uint32_t vertex)
	{
		const Submesh& submesh = mesh.submeshes[submeshIndex];

		const VertexAttribute* tangent = nullptr;
		for (uint8_t a = 0; a < submesh.layout.attributeCount; ++a)
		{
			if (submesh.layout.attributes[a].semantic == VertexSemantic::kTangent)
				tangent = &submesh.layout.attributes[a];
		}

		INFO("submesh " << submeshIndex);
		REQUIRE(tangent != nullptr);

		auto value = glm::vec4();
		std::memcpy(
			&value,
			mesh.vertexData.data() + submesh.vertexByteOffset +
				static_cast<size_t>(vertex) * submesh.layout.stride + tangent->offset,
			sizeof(value));

		return value;
	}
}

// The offsets and counts are the file's claim about its buffers, not a fact about them -- and
// `assetlib_cli tangents` hands this a .bmesh named on a command line.
TEST_CASE("A submesh whose ranges run past their pools is refused", "[tangents]")
{
	SECTION("indices")
	{
		BMesh mesh                   = OneTriangle();
		mesh.submeshes[0].indexCount = 4096;
		CHECK_THROWS_AS(generateTangents(mesh), std::runtime_error);
	}

	SECTION("vertices")
	{
		BMesh mesh                    = OneTriangle();
		mesh.submeshes[0].vertexCount = 4096;
		CHECK_THROWS_AS(generateTangents(mesh), std::runtime_error);
	}
}

TEST_CASE("A generated tangent follows the surface's U direction", "[tangents]")
{
	BMesh mesh = OneTriangle();

	const TangentGenResult result = generateTangents(mesh);

	REQUIRE(result.generated == 1);
	REQUIRE(hasTangent(mesh.submeshes[0]));

	// UVs run with +X and +Y on a triangle facing +Z, so the frame is exactly the world axes: this
	// is the one case where the right answer can be written down rather than merely asserted about.
	for (uint32_t v = 0; v < 3; ++v)
	{
		const glm::vec4 tangent = TangentOf(mesh, 0, v);

		INFO("vertex " << v);
		CHECK(tangent.x == Catch::Approx(1.0f).margin(1e-5));
		CHECK(tangent.y == Catch::Approx(0.0f).margin(1e-5));
		CHECK(tangent.z == Catch::Approx(0.0f).margin(1e-5));

		// cross(N, T) = cross(+Z, +X) = +Y, which is the bitangent the UVs imply, so w is positive.
		CHECK(tangent.w == Catch::Approx(1.0f));
	}
}

TEST_CASE("A mirrored UV shell is marked by the tangent's handedness", "[tangents]")
{
	BMesh mesh = OneTriangle();

	// Flip V, which mirrors the shell: the bitangent now runs against cross(N, T), and the shader
	// reads that from w. Getting the sign wrong leans every normal map the wrong way.
	const size_t uvOffset = 24;
	for (uint32_t v = 0; v < 3; ++v)
	{
		auto uv = glm::vec2();
		std::memcpy(&uv, mesh.vertexData.data() + v * 32 + uvOffset, sizeof(uv));
		uv.y = -uv.y;
		std::memcpy(mesh.vertexData.data() + v * 32 + uvOffset, &uv, sizeof(uv));
	}

	REQUIRE(generateTangents(mesh).generated == 1);

	CHECK(TangentOf(mesh, 0, 0).w == Catch::Approx(-1.0f));
}

TEST_CASE("A mesh that already has tangents is left alone", "[tangents]")
{
	BMesh mesh = OneTriangle(true);

	const std::vector<std::byte> before = mesh.vertexData;
	const TangentGenResult       result = generateTangents(mesh);

	CHECK(result.generated == 0);
	CHECK(result.kept == 1);

	// Byte for byte: re-deriving would overwrite an authored basis, which is the one thing the
	// authored one is there to prevent.
	CHECK(mesh.vertexData == before);
}

TEST_CASE("A submesh with no UVs cannot have a tangent derived", "[tangents]")
{
	BMesh mesh = OneTriangle();

	// Drop the UVs, leaving position and normal. A tangent is a UV-space derivative, so there is
	// nothing to derive from -- and a mesh with no UVs samples no normal map either.
	mesh.submeshes[0].layout.attributeCount = 2;

	const TangentGenResult result = generateTangents(mesh);

	CHECK(result.generated == 0);
	CHECK(result.skipped == 1);
	CHECK_FALSE(hasTangent(mesh.submeshes[0]));
}

TEST_CASE("Generating for one submesh moves the ones after it", "[tangents]")
{
	// Two submeshes in one pool, the first gaining a tangent -- so the second's vertices shift by
	// the 16 bytes the first grew by. A stale offset here would read another submesh's vertices as
	// its own, which renders as garbage rather than as an error.
	BMesh mesh = OneTriangle();

	Submesh second          = mesh.submeshes[0];
	second.vertexByteOffset = static_cast<uint32_t>(mesh.vertexData.size());
	mesh.vertexData.insert(mesh.vertexData.end(), mesh.vertexData.begin(), mesh.vertexData.end());
	mesh.submeshes.push_back(second);

	REQUIRE(generateTangents(mesh).generated == 2);

	CHECK(mesh.submeshes[0].vertexByteOffset == 0);
	CHECK(mesh.submeshes[0].layout.stride == 48);
	CHECK(mesh.submeshes[1].vertexByteOffset == 48 * 3);
	CHECK(mesh.submeshes[1].layout.stride == 48);

	// And the second submesh's own vertices came through, not the first's tail.
	CHECK(TangentOf(mesh, 1, 0).x == Catch::Approx(1.0f).margin(1e-5));
}

TEST_CASE("A tangent is appended without disturbing the attributes before it", "[tangents]")
{
	BMesh mesh = OneTriangle();

	REQUIRE(generateTangents(mesh).generated == 1);

	const VertexLayout& layout = mesh.submeshes[0].layout;
	REQUIRE(layout.attributeCount == 4);

	// Existing offsets are untouched, so the blob is copied through rather than repacked -- and a
	// consumer that cached the position offset still reads positions.
	CHECK(layout.attributes[0].offset == 0);
	CHECK(layout.attributes[1].offset == 12);
	CHECK(layout.attributes[2].offset == 24);
	CHECK(layout.attributes[3].offset == 32);
	CHECK(layout.stride == 48);

	auto position = glm::vec3();
	std::memcpy(&position, mesh.vertexData.data() + layout.stride, sizeof(position));
	CHECK(position.x == Catch::Approx(1.0f));
}
