#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/ISceneView.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_test_macros.hpp>

// The one control a view has over its occlusion cull: what SetOcclusionCulling keeps, and that a
// view starts culling. What the cull does with it is CullInstances' and Forward World's to prove.

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

TEST_CASE(
	"a view starts culling its occludees, and keeps the setting it is given",
	"[culling][occlusion][contract]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);

	CHECK(view->GetOcclusionCulling());

	view->SetOcclusionCulling(false);
	CHECK_FALSE(view->GetOcclusionCulling());

	view->SetOcclusionCulling(true);
	CHECK(view->GetOcclusionCulling());
}
