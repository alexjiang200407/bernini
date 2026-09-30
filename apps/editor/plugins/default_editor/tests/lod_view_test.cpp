#include "Windows/MeshEditor/lod_view.h"

#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <bgl/glm.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <vector>

// What the Mesh Editor's level-of-detail selector lists of a mesh and says Auto draws, pinned without a
// window or a device. That the level read here is the one the GPU draws is bgl's
// LodSelect_test, against the same bgl/lod_select.h.

namespace
{
	assetlib::Submesh
	Box(uint32_t triangles, const glm::vec3& minBound, const glm::vec3& maxBound)
	{
		auto submesh       = assetlib::Submesh();
		submesh.indexCount = triangles * 3;
		submesh.aabbMin    = minBound;
		submesh.aabbMax    = maxBound;
		return submesh;
	}

	// Two submeshes a level, drawn from 30, 10 and 3 pixels. Level 0 spans -1..1; the coarser
	// levels are larger on purpose, since only level 0's bound is what a size is measured by.
	assetlib::BMesh
	ThreeLevels()
	{
		auto mesh = assetlib::BMesh();
		mesh.meshes.push_back(
			{ .firstSubmesh = 0,
		      .submeshCount = 2,
		      .nameOffset   = 0,
		      .lodCount     = 3,
		      .firstLod     = 0 });
		mesh.submeshes = {
			Box(12, glm::vec3(-1.0f), glm::vec3(0.0f)), Box(6, glm::vec3(0.0f), glm::vec3(1.0f)),
			Box(6, glm::vec3(-5.0f), glm::vec3(5.0f)),  Box(3, glm::vec3(-5.0f), glm::vec3(5.0f)),
			Box(2, glm::vec3(-5.0f), glm::vec3(5.0f)),  Box(1, glm::vec3(-5.0f), glm::vec3(5.0f)),
		};
		mesh.lods = { { 30.0f }, { 10.0f }, { 3.0f } };
		return mesh;
	}

	// 32 pixels a unit at distance one, so a sqrt(3) sphere spans 110.85 / d pixels at distance d.
	constexpr float c_PixelsPerUnit = 32.0f;
	constexpr float c_PixelsAtOne   = 110.851f;

	glm::mat4
	At(float pixels)
	{
		return glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -c_PixelsAtOne / pixels));
	}

	editor::LodReadout
	Read(
		const editor::MeshLods& lods,
		float                   pixels,
		std::optional<uint32_t> forced   = std::nullopt,
		std::optional<uint32_t> previous = std::nullopt)
	{
		return editor::ReadLod(
			lods,
			At(pixels),
			glm::vec3(0.0f),
			c_PixelsPerUnit,
			1.0f,
			forced,
			previous);
	}
}

TEST_CASE("A mesh lists the size each level is drawn from", "[mesheditor][lod]")
{
	const editor::MeshLods lods = editor::LodsOf(ThreeLevels(), 0);

	CHECK(lods.minPixels == std::vector<float>{ 30.0f, 10.0f, 3.0f });
	CHECK(glm::vec3(lods.levelZeroSphere) == glm::vec3(0.0f));
	CHECK(lods.levelZeroSphere.w == Catch::Approx(glm::sqrt(3.0f)));

	SECTION("a container with no table is one level drawn at every size")
	{
		auto mesh               = ThreeLevels();
		mesh.meshes[0].lodCount = 1;
		mesh.lods.clear();
		const editor::MeshLods one = editor::LodsOf(mesh, 0);
		CHECK(one.minPixels == std::vector<float>{ 0.0f });
	}

	SECTION("a mesh whose levels run past its submeshes lists nothing")
	{
		auto mesh = ThreeLevels();
		mesh.submeshes.resize(5);
		CHECK(editor::LodsOf(mesh, 0).minPixels.empty());
		CHECK(editor::LodsOf(mesh, 1).minPixels.empty());
	}
}

TEST_CASE("Auto reads the level the size on screen earns", "[mesheditor][lod]")
{
	const editor::MeshLods lods = editor::LodsOf(ThreeLevels(), 0);

	const editor::LodReadout near = Read(lods, 55.0f);
	CHECK(near.level == 0);
	CHECK(near.pixels == Catch::Approx(55.0f).epsilon(0.001));
	CHECK(Read(lods, 22.0f).level == 1);
	CHECK(Read(lods, 5.5f).level == 2);

	SECTION("below the last level's size is the draw-nothing tier")
	{
		CHECK(Read(lods, 2.2f).level == 3);
	}

	SECTION("a level held last is kept in the gap above a finer one's size")
	{
		CHECK(Read(lods, 32.0f, std::nullopt, 1u).level == 1);
		CHECK(Read(lods, 32.0f).level == 0);
	}

	SECTION("a pinned level is read as pinned, whatever the size")
	{
		CHECK(Read(lods, 2.2f, 1u).level == 1);
		CHECK(Read(lods, 2.2f, 1u).pixels == Catch::Approx(2.2f).epsilon(0.001));
	}

	SECTION("a level past the mesh's is its coarsest, never nothing")
	{
		CHECK(Read(lods, 55.0f, 7u).level == 2);
	}
}
