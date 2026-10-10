#include "scene/Scene.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/idl/Geom.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/MeshVertex.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <span>

namespace
{
	bgl::test::GraphicsSetup
	HeadlessOptions()
	{
		auto opts                        = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer = false;
		return opts;
	}

	bgl::MeshVertex
	Up(const glm::vec3 pos)
	{
		return { .pos     = pos,
			     .normal  = glm::vec3(0.0f, 1.0f, 0.0f),
			     .uv      = glm::vec2(pos.x, pos.z),
			     .tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) };
	}

	// A square on the ground from (0, 0) to (4, 4), raised 2 at one corner, as two triangles.
	const std::array c_Vertices = {
		Up(glm::vec3(0.0f, 0.0f, 0.0f)),
		Up(glm::vec3(4.0f, 0.0f, 0.0f)),
		Up(glm::vec3(4.0f, 2.0f, 4.0f)),
		Up(glm::vec3(0.0f, 0.0f, 4.0f)),
	};
	constexpr std::array<uint32_t, 6> c_Indices = { { 0, 3, 2, 0, 2, 1 } };
}

TEST_CASE("A triangle list is a geom bounded by its vertices", "[geom]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(bgl::SceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const auto geom = scene->AddTriangleGeom(
		c_Vertices,
		c_Indices,
		scene->CreatePbrMaterial(bgl::PbrMaterialDesc()));
	REQUIRE(geom.IsValid());
	CHECK(scene->GetGeomSubmeshes(geom.handle.index).submeshCount == 1u);

	const glm::vec4 sphere =
		scene->GetGeomBuffer()[scene->GetGeomEntry(geom.handle.index)].boundingSphere;
	for (const bgl::MeshVertex& v : c_Vertices)
		CHECK(glm::distance(glm::vec3(sphere), v.pos) <= sphere.w + 1e-4f);
}

TEST_CASE("A triangle list that is not one is refused", "[geom]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(bgl::SceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const auto partial  = std::span(c_Indices).first(4);
	const auto pastEnd  = std::array<uint32_t, 3>{ { 0, 1, 4 } };
	const auto vertices = std::span(c_Vertices);

	CHECK_THROWS_AS(scene->AddTriangleGeom(vertices, {}), bgl::SceneError);
	CHECK_THROWS_AS(scene->AddTriangleGeom(vertices, partial), bgl::SceneError);
	CHECK_THROWS_AS(scene->AddTriangleGeom(vertices, pastEnd), bgl::SceneError);
}
