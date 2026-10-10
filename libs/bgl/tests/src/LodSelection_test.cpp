#include "gfx/Frustum.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/util.h"
#include <bgl/IGraphics.h>
#include <bgl/ISceneView.h>
#include <bgl/LodLevel.h>
#include <bgl/error.h>
#include <bgl/idl/Constants.h>
#include <bgl/idl/CullView.h>
#include <bgl/types/Camera.h>
#include <bgl/types/InstanceDesc.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

// The one control a view has over its levels of detail: what SetLodSelection keeps, what it
// refuses, and that a view starts as authored. What the choice does to a frame is the cull's and
// the forward stages' to prove, not this file's.

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
}

TEST_CASE("a view starts drawing every mesh as authored", "[lod][contract]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);

	const bgl::LodSelectionDesc selection = view->GetLodSelection();
	CHECK(selection.pixelScale == 1.0f);
	CHECK_FALSE(selection.forceLevel.has_value());
	CHECK(selection.fadeSeconds == bgl::LodSelectionDesc().fadeSeconds);
	CHECK(selection.poseBudget == bgl::LodSelectionDesc().poseBudget);
	CHECK(selection.posePixels == bgl::LodSelectionDesc().posePixels);
	CHECK_FALSE(selection.forcePoseSource.has_value());
	CHECK_FALSE(selection.forceImpostor);
}

TEST_CASE("a view keeps the selection it was given", "[lod][contract]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);

	auto desc            = bgl::LodSelectionDesc();
	desc.pixelScale      = 0.5f;
	desc.forceLevel      = bgl::LodLevel::kLod2;
	desc.fadeSeconds     = 0.0f;
	desc.poseBudget      = 0;
	desc.posePixels      = 40.0f;
	desc.forcePoseSource = bgl::PoseSource::kBoneAnimTable;
	CHECK_NOTHROW(view->SetLodSelection(desc));

	const bgl::LodSelectionDesc read = view->GetLodSelection();
	CHECK(read.pixelScale == 0.5f);
	REQUIRE(read.forceLevel.has_value());
	CHECK(*read.forceLevel == bgl::LodLevel::kLod2);
	CHECK(read.fadeSeconds == 0.0f);
	CHECK(read.poseBudget == 0u);
	CHECK(read.posePixels == 40.0f);
	CHECK(read.forcePoseSource == bgl::PoseSource::kBoneAnimTable);

	SECTION("a later write replaces the whole record")
	{
		CHECK_NOTHROW(view->SetLodSelection(bgl::LodSelectionDesc()));
		CHECK_FALSE(view->GetLodSelection().forceLevel.has_value());
		CHECK(view->GetLodSelection().pixelScale == 1.0f);
	}

	SECTION("the last level a mesh may carry can be forced")
	{
		desc.forceLevel = bgl::LodLevel::kLod7;
		CHECK_NOTHROW(view->SetLodSelection(desc));
	}
}

TEST_CASE("a selection no cull could act on is refused, and the old one kept", "[lod][contract]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
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
		desc.forceLevel = bgl::LodLevel::kCount;
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
	}

	SECTION("forcing the impostor tier and a level at once")
	{
		auto desc          = bgl::LodSelectionDesc();
		desc.forceImpostor = true;
		CHECK_NOTHROW(view->SetLodSelection(desc));
		view->SetLodSelection(kept);
		desc.forceLevel = bgl::LodLevel::kLod0;
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
	}

	SECTION("a pose threshold that is zero, negative or not finite")
	{
		auto desc       = bgl::LodSelectionDesc();
		desc.posePixels = 0.0f;
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
		desc.posePixels = -1.0f;
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
		desc.posePixels = std::nanf("");
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
	}

	SECTION("forcing the automatic source, which is a choice rather than a source")
	{
		auto desc            = bgl::LodSelectionDesc();
		desc.forcePoseSource = bgl::PoseSource::kAuto;
		CHECK_THROWS_AS(view->SetLodSelection(desc), bgl::SceneError);
	}

	CHECK(view->GetLodSelection().pixelScale == 2.0f);
}

TEST_CASE("a draw resolves the view's selection into the cull view it uploads", "[lod][culling]")
{
	const bgl::idl::CullView built = bgl::BuildCullView(glm::mat4(1.0f));
	const auto               eye   = glm::vec3(1.0f, 2.0f, 3.0f);

	SECTION("as authored")
	{
		auto view = built;
		bgl::ResolveLodSelection(view, bgl::LodSelectionDesc(), eye, 540.0f, 0.03f);

		CHECK(view.cameraPosAndPixelsPerUnit == glm::vec4(eye, 540.0f));
		CHECK(view.lodPixelScale == 1.0f);
		CHECK(view.lodForcedLevel == bgl::idl::cLodForceNone);
		CHECK(view.lodFadeStep == Catch::Approx(0.03f / 0.15f));
		CHECK(view.viewProj == built.viewProj);
		CHECK(view.posePixels == bgl::LodSelectionDesc().posePixels);
		CHECK(view.poseBudget == bgl::LodSelectionDesc().poseBudget);
		CHECK(view.poseForced == bgl::idl::cPoseForceNone);
	}

	SECTION("a forced pose source")
	{
		auto desc            = bgl::LodSelectionDesc();
		desc.forcePoseSource = bgl::PoseSource::kBoneAnimTable;

		auto view = built;
		bgl::ResolveLodSelection(view, desc, eye, 540.0f, 0.03f);
		CHECK(view.poseForced == bgl::idl::cPoseForceTable);

		desc.forcePoseSource = bgl::PoseSource::kPerInstance;
		bgl::ResolveLodSelection(view, desc, eye, 540.0f, 0.03f);
		CHECK(view.poseForced == bgl::idl::cPoseForcePerInstance);
	}

	SECTION("scaled, forced and swapped")
	{
		auto desc        = bgl::LodSelectionDesc();
		desc.pixelScale  = 2.0f;
		desc.forceLevel  = bgl::LodLevel::kLod3;
		desc.fadeSeconds = 0.0f;

		auto view = built;
		bgl::ResolveLodSelection(view, desc, eye, 540.0f, 0.03f);

		CHECK(view.lodPixelScale == 2.0f);
		CHECK(view.lodForcedLevel == 3u);
		CHECK(view.lodFadeStep == 1.0f);
	}

	SECTION("a clock that did not advance completes a dissolve at once")
	{
		auto view = built;
		bgl::ResolveLodSelection(view, bgl::LodSelectionDesc(), eye, 540.0f, 0.0f);
		CHECK(view.lodFadeStep == 1.0f);
	}
}
