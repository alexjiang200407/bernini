#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/MaterialType.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_test_macros.hpp>

// Per-submesh selection on a SceneView: the marks, their queries, and the dense-index list the
// selection-mask draw dispatches over.

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

	bgl::SceneDesc
	SmallSceneDesc()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 8;
		desc.initialSubmeshes            = 8;
		desc.initialMeshlets             = 100;
		desc.initialVertexBufferByteSize = 40000;
		desc.initialIndices              = 1000;
		return desc;
	}
}

TEST_CASE("Submesh selection marks", "[selection][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(SmallSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto  viewHandle = gfx->CreateSceneView(sceneHandle, 8);
	auto* view       = viewHandle->As<bgl::SceneView>();
	REQUIRE(view != nullptr);

	auto material         = bgl::MaterialHandle();
	material.materialType = bgl::MaterialType::kPBR;

	auto geom = scene->AddCubeGeom(material);
	REQUIRE(geom.IsValid());

	auto first  = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
	auto second = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));

	SECTION("An instance starts unselected")
	{
		CHECK_FALSE(view->IsSubmeshSelected(first, 0));
		CHECK_FALSE(view->IsSubmeshSelected(second, 0));
		CHECK(view->GetSelectedInstances().empty());
	}

	SECTION("Set and unset round-trip")
	{
		view->SetSubmeshSelected(first, 0, true);
		CHECK(view->IsSubmeshSelected(first, 0));
		CHECK_FALSE(view->IsSubmeshSelected(second, 0));

		view->SetSubmeshSelected(first, 0, false);
		CHECK_FALSE(view->IsSubmeshSelected(first, 0));
		CHECK(view->GetSelectedInstances().empty());
	}

	SECTION("ClearSelection unmarks every instance")
	{
		view->SetSubmeshSelected(first, 0, true);
		view->SetSubmeshSelected(second, 0, true);
		REQUIRE(view->GetSelectedInstances().size() == 2);

		view->ClearSelection();
		CHECK_FALSE(view->IsSubmeshSelected(first, 0));
		CHECK_FALSE(view->IsSubmeshSelected(second, 0));
		CHECK(view->GetSelectedInstances().empty());
	}

	SECTION("The selected list holds dense instance-buffer indices")
	{
		view->SetSubmeshSelected(first, 0, true);
		view->SetSubmeshSelected(second, 0, true);

		// The cube has one submesh, so instance i's sole drawable sits at dense index i.
		const auto both = view->GetSelectedInstances();
		REQUIRE(both.size() == 2);
		CHECK(both[0] == 0);
		CHECK(both[1] == 1);
	}

	SECTION("Deleting an instance re-resolves the survivors' dense indices")
	{
		view->SetSubmeshSelected(first, 0, true);
		view->SetSubmeshSelected(second, 0, true);
		REQUIRE(view->GetSelectedInstances().size() == 2);

		// The erase swap-moves the second instance's drawable from dense index 1 to 0; a list
		// that cached the old index would now dispatch a stale slot.
		view->DeleteMeshInstance(first);
		CHECK(view->IsSubmeshSelected(second, 0));

		const auto survivors = view->GetSelectedInstances();
		REQUIRE(survivors.size() == 1);
		CHECK(survivors[0] == 0);
	}

	SECTION("A new instance in a reused slot starts unselected")
	{
		view->SetSubmeshSelected(first, 0, true);
		view->DeleteMeshInstance(first);

		auto reused = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
		CHECK_FALSE(view->IsSubmeshSelected(reused, 0));

		const auto selected = view->GetSelectedInstances();
		CHECK(selected.empty());
	}

	SECTION("Invalid arguments throw SceneError")
	{
		CHECK_THROWS_AS(
			view->SetSubmeshSelected(bgl::MeshInstanceHandle(), 0, true),
			bgl::SceneError);
		CHECK_THROWS_AS(view->IsSubmeshSelected(bgl::MeshInstanceHandle(), 0), bgl::SceneError);

		// The cube has exactly one submesh.
		CHECK_THROWS_AS(view->SetSubmeshSelected(first, 1, true), bgl::SceneError);
		CHECK_THROWS_AS(view->IsSubmeshSelected(first, 1), bgl::SceneError);

		view->DeleteMeshInstance(first);
		CHECK_THROWS_AS(view->SetSubmeshSelected(first, 0, true), bgl::SceneError);
		CHECK_THROWS_AS(view->IsSubmeshSelected(first, 0), bgl::SceneError);
	}
}
