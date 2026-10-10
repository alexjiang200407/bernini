#include "scene/Scene.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <array>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/idl/Geom.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/TriangleGeomDesc.h>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
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

	// A vertex that carries only what a flat, unlit-by-normal surface reads: where it is and a UV.
	struct PositionUv
	{
		glm::vec3 pos;
		glm::vec2 uv;
	};

	using assetlib::VertexFormat;
	using assetlib::VertexSemantic;

	[[nodiscard]] assetlib::VertexLayout
	PositionUvLayout()
	{
		auto layout           = assetlib::VertexLayout();
		layout.attributeCount = 2;
		layout.stride         = sizeof(PositionUv);
		layout.attributes[0]  = { VertexSemantic::kPosition, VertexFormat::kFloat32x3, 0 };
		layout.attributes[1]  = { VertexSemantic::kTexCoord0, VertexFormat::kFloat32x2, 12 };
		return layout;
	}

	// A square on the ground from (0, 0) to (4, 4), raised 2 at one corner, as two triangles.
	const std::array<PositionUv, 4>   c_Vertices = { {
		{ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.0f, 0.0f) },
		{ glm::vec3(4.0f, 0.0f, 0.0f), glm::vec2(1.0f, 0.0f) },
		{ glm::vec3(4.0f, 2.0f, 4.0f), glm::vec2(1.0f, 1.0f) },
		{ glm::vec3(0.0f, 0.0f, 4.0f), glm::vec2(0.0f, 1.0f) },
	} };
	constexpr std::array<uint32_t, 6> c_Indices  = { { 0, 3, 2, 0, 2, 1 } };

	[[nodiscard]] bgl::TriangleGeomDesc
	Square()
	{
		return bgl::TriangleGeomDesc()
		    .SetVertices(std::as_bytes(std::span(c_Vertices)))
		    .SetLayout(PositionUvLayout())
		    .SetIndices(c_Indices);
	}
}

TEST_CASE("A triangle list of any layout is a geom bounded by its positions", "[geom]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(bgl::SceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const auto geom = scene->AddTriangleGeom(
		Square().SetMaterial(scene->CreatePbrMaterial(bgl::PbrMaterialDesc())));
	REQUIRE(geom.IsValid());
	CHECK(scene->GetGeomSubmeshes(geom.handle.index).submeshCount == 1u);

	const glm::vec4 sphere =
		scene->GetGeomBuffer()[scene->GetGeomEntry(geom.handle.index)].boundingSphere;
	for (const PositionUv& v : c_Vertices)
		CHECK(glm::distance(glm::vec3(sphere), v.pos) <= sphere.w + 1e-4f);
}

TEST_CASE("A triangle list its layout cannot describe is refused", "[geom]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(bgl::SceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const auto refused = [&](const bgl::TriangleGeomDesc& desc) {
		CHECK_THROWS_AS(scene->AddTriangleGeom(desc), bgl::SceneError);
	};

	const auto pastEnd = std::array<uint32_t, 3>{ { 0, 1, 4 } };
	refused(Square().SetIndices({}));
	refused(Square().SetIndices(std::span(c_Indices).first(4)));
	refused(Square().SetIndices(pastEnd));
	refused(
		Square().SetVertices(
			std::as_bytes(std::span(c_Vertices)).first(sizeof(PositionUv) * 3 + 4)));

	auto noPosition                   = PositionUvLayout();
	noPosition.attributes[0].semantic = VertexSemantic::kNormal;
	refused(Square().SetLayout(noPosition));

	auto skinned          = PositionUvLayout();
	skinned.attributes[1] = { VertexSemantic::kWeights0, VertexFormat::kUnorm16x2, 12 };
	refused(Square().SetLayout(skinned));

	auto overrun                 = PositionUvLayout();
	overrun.attributes[1].format = VertexFormat::kFloat32x4;
	refused(Square().SetLayout(overrun));

	auto unaligned                 = PositionUvLayout();
	unaligned.attributes[1].offset = 13;
	unaligned.attributes[1].format = VertexFormat::kUnorm8x4;
	refused(Square().SetLayout(unaligned));
}
