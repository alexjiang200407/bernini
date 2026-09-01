#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

// The temporal epoch: what a view reports to the frame drawing it when it has changed in a way no
// motion vector describes, so the TAA resolve takes that frame whole instead of reprojecting into
// it. The resolve's own half of that is measured in TaaResolve_test; this pins which mutations
// count, which is the part a caller can break without any image looking wrong until it does.

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

	// A frame drawn: takes whatever break is pending, so the next call reports only what the lines
	// between them did.
	void
	Consume(bgl::SceneView* view) noexcept
	{
		static_cast<void>(view->AdvanceTemporalEpoch());
	}
}

TEST_CASE("Placing or deleting an instance breaks the temporal continuity", "[scene][taa]")
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

	const auto geom = scene->AddCubeGeom(material);
	REQUIRE(geom.IsValid());

	// Whatever the setup moved, drawn and consumed -- from here a report is this test's doing.
	Consume(view);
	REQUIRE_FALSE(view->AdvanceTemporalEpoch());

	SECTION("A placement is reported once, to the frame that draws after it")
	{
		const auto instance = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
		REQUIRE(instance.IsValid());

		CHECK(view->AdvanceTemporalEpoch());

		// Once, not until something else happens: the frame after the break reprojects normally.
		CHECK_FALSE(view->AdvanceTemporalEpoch());
	}

	SECTION("A deletion is reported")
	{
		const auto instance = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
		Consume(view);

		view->DeleteMeshInstance(instance);

		CHECK(view->AdvanceTemporalEpoch());
		CHECK_FALSE(view->AdvanceTemporalEpoch());
	}

	// What the Animation panel's clip switch is: there is no mutate-instance API, so it destroys and
	// respawns. Both halves happen before the next frame, and the frame after them must see the
	// break -- the pose that arrives is the new clip's, against history holding the old clip's.
	SECTION("A destroy-and-respawn is one break, seen by the next frame")
	{
		const auto instance = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
		Consume(view);

		view->DeleteMeshInstance(instance);
		view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));

		CHECK(view->AdvanceTemporalEpoch());
		CHECK_FALSE(view->AdvanceTemporalEpoch());
	}

	// The boundary that keeps the epoch affordable. A selection mark is per-frame editor state and
	// changes no pixel's provenance; if it counted, dragging a selection would leave the viewport
	// permanently unaccumulated -- which is the failure the epoch's "discrete rebinds only" rule
	// exists to prevent, and the one a new bump is most likely to reintroduce.
	SECTION("Selecting a submesh is not a break")
	{
		const auto instance = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
		Consume(view);

		view->SetSubmeshSelected(instance, 0, true);

		CHECK_FALSE(view->AdvanceTemporalEpoch());
	}
}
