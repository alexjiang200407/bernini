#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/ISceneView.h>
#include <bgl/error.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl_common/idl/Constants.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

// The one control a view has over its levels of detail: what SetLodSelection keeps, what it
// refuses, and that a view starts as authored. What the choice does to a frame is the cull's and
// the forward stages' to prove, not this file's.

namespace
{
	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
		return opts;
	}
}

TEST_CASE("a view starts drawing every mesh as authored", "[lod][contract]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);

	const bgl::LodSelectionDesc selection = view->GetLodSelection();
	CHECK(selection.pixelScale == 1.0f);
	CHECK_FALSE(selection.forceLevel.has_value());
	CHECK(selection.fadeSeconds == bgl::LodSelectionDesc().fadeSeconds);
}

TEST_CASE("a view keeps the selection it was given", "[lod][contract]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);

	auto desc        = bgl::LodSelectionDesc();
	desc.pixelScale  = 0.5f;
	desc.forceLevel  = 2u;
	desc.fadeSeconds = 0.0f;
	CHECK_NOTHROW(view->SetLodSelection(desc));

	const bgl::LodSelectionDesc read = view->GetLodSelection();
	CHECK(read.pixelScale == 0.5f);
	REQUIRE(read.forceLevel.has_value());
	CHECK(*read.forceLevel == 2u);
	CHECK(read.fadeSeconds == 0.0f);

	SECTION("a later write replaces the whole record")
	{
		CHECK_NOTHROW(view->SetLodSelection(bgl::LodSelectionDesc()));
		CHECK_FALSE(view->GetLodSelection().forceLevel.has_value());
		CHECK(view->GetLodSelection().pixelScale == 1.0f);
	}

	SECTION("the last level a mesh may carry can be forced")
	{
		desc.forceLevel = bgl::idl::cMaxMeshLods - 1u;
		CHECK_NOTHROW(view->SetLodSelection(desc));
	}
}

TEST_CASE("a selection no cull could act on is refused, and the old one kept", "[lod][contract]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);

	auto kept       = bgl::LodSelectionDesc();
	kept.pixelScale = 2.0f;
	view->SetLodSelection(kept);

	SECTION("a scale that is zero, negative or not finite")
	{
		auto desc       = bgl::LodSelectionDesc();
		desc.pixelScale = 0.0f;
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
		desc.pixelScale = -1.0f;
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
		desc.pixelScale = std::numeric_limits<float>::infinity();
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
		desc.pixelScale = std::nanf("");
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
	}

	SECTION("a fade that is negative or not finite")
	{
		auto desc        = bgl::LodSelectionDesc();
		desc.fadeSeconds = -0.1f;
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
		desc.fadeSeconds = std::numeric_limits<float>::infinity();
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
	}

	SECTION("a forced level no mesh can carry")
	{
		auto desc       = bgl::LodSelectionDesc();
		desc.forceLevel = bgl::idl::cMaxMeshLods;
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
	}

	CHECK(view->GetLodSelection().pixelScale == 2.0f);
}
