#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/TestOptions.h"
#include "util/util.h"
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/types/SceneDesc.h>
#include <bgl_common/idl/MeshInstance.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// What SetInstanceTransform records, as opposed to what it draws. The rollover is the whole of the
// difference between a correct motion vector and a one-frame wobble, and none of it needs a pixel:
// the previous transform is a field, and these cases read it.
//
// The rendered half is in MotionVectors_test.

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
	TestSceneDesc()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 4;
		desc.initialSubmeshes            = 8;
		desc.initialMeshlets             = 32;
		desc.initialVertexBufferByteSize = 65536;
		desc.initialIndices              = 1024;
		desc.initialPbrMaterials         = 4;
		return desc;
	}

	glm::mat4
	At(float x)
	{
		return glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f));
	}

	// The two transforms a placement straddles. Read off the CPU mirror, which is the bytes Update()
	// uploads.
	struct Straddle
	{
		glm::mat4 current;
		glm::mat4 previous;
	};

	Straddle
	StraddleOf(bgl::SceneView* view, bgl::MeshInstanceHandle instance)
	{
		const bgl::idl::MeshInstance& mesh = view->GetMeshBuffer().AtIndex(instance.handle.index);

		auto previous         = bgl::idl::MeshInstance();
		previous.transform[0] = mesh.prevTransform[0];
		previous.transform[1] = mesh.prevTransform[1];
		previous.transform[2] = mesh.prevTransform[2];

		return Straddle{ bgl::ReadInstanceTransform(mesh), bgl::ReadInstanceTransform(previous) };
	}

	struct Fixture
	{
		bgl::GraphicsRef  gfx;
		bgl::SceneRef     scene;
		bgl::SceneViewRef view;
		bgl::SceneView*   viewImpl = nullptr;

		Fixture()
		{
			gfx = bgl::CreateGraphics(HeadlessOptions());
			REQUIRE(gfx != nullptr);

			scene = gfx->CreateScene(TestSceneDesc());
			view  = gfx->CreateSceneView(scene, 8);

			viewImpl = view->As<bgl::SceneView>();
			REQUIRE(viewImpl != nullptr);
		}

		bgl::MeshInstanceHandle
		Place(float x)
		{
			auto* impl = scene->As<bgl::Scene>();
			return view->CreateStaticMeshInstance(impl->AddCubeGeom(), At(x));
		}
	};
}

TEST_CASE("A placement spawns with no velocity", "[transform]")
{
	auto       fixture  = Fixture();
	const auto instance = fixture.Place(3.0f);

	const auto straddle = StraddleOf(fixture.viewImpl, instance);

	// Not merely close: a placement whose previous transform were left at identity would render a
	// full-scene smear on its first frame.
	CHECK(straddle.current == At(3.0f));
	CHECK(straddle.previous == At(3.0f));
}

TEST_CASE("A write straddles the frame it was drawn on", "[transform]")
{
	auto       fixture  = Fixture();
	const auto instance = fixture.Place(0.0f);

	fixture.viewImpl->SetInstanceTransform(instance, At(1.0f));

	const auto straddle = StraddleOf(fixture.viewImpl, instance);
	CHECK(straddle.current == At(1.0f));
	CHECK(straddle.previous == At(0.0f));
}

TEST_CASE("Written twice in one frame, the previous transform is what was drawn", "[transform]")
{
	auto       fixture  = Fixture();
	const auto instance = fixture.Place(0.0f);

	fixture.viewImpl->SetInstanceTransform(instance, At(1.0f));
	fixture.viewImpl->SetInstanceTransform(instance, At(2.0f));

	// Not At(1.0f). The intermediate was never drawn, so reprojecting through it would report a
	// velocity for a motion no frame ever showed -- which is what a caller that recomputes a
	// position twice in a frame would otherwise pay for.
	const auto straddle = StraddleOf(fixture.viewImpl, instance);
	CHECK(straddle.current == At(2.0f));
	CHECK(straddle.previous == At(0.0f));
}

TEST_CASE("A placement that stops moving returns to zero velocity", "[transform]")
{
	auto       fixture  = Fixture();
	const auto instance = fixture.Place(0.0f);

	fixture.viewImpl->SetInstanceTransform(instance, At(1.0f));

	// The frame the move was drawn on: it still straddles, because that is the frame the velocity
	// describes.
	fixture.viewImpl->AdvanceInstanceTransforms(1);
	{
		const auto straddle = StraddleOf(fixture.viewImpl, instance);
		CHECK(straddle.current == At(1.0f));
		CHECK(straddle.previous == At(0.0f));
	}

	// The next frame, with no write: prev comes up to current, so the surface is static again
	// rather than reporting last frame's motion a second time.
	fixture.viewImpl->AdvanceInstanceTransforms(2);
	{
		const auto straddle = StraddleOf(fixture.viewImpl, instance);
		CHECK(straddle.current == At(1.0f));
		CHECK(straddle.previous == At(1.0f));
	}
}

// The case a placement moved every frame is in, and the one every other case here misses by
// writing at most once: each frame must straddle the frame before it, not collapse onto itself.
TEST_CASE("A placement written on consecutive frames keeps straddling", "[transform]")
{
	auto       fixture  = Fixture();
	const auto instance = fixture.Place(0.0f);

	for (uint32_t frame = 1; frame <= 4; ++frame)
	{
		const auto here   = static_cast<float>(frame);
		const auto before = here - 1.0f;

		fixture.viewImpl->SetInstanceTransform(instance, At(here));
		fixture.viewImpl->AdvanceInstanceTransforms(frame);

		INFO("frame " << frame);
		const auto straddle = StraddleOf(fixture.viewImpl, instance);
		CHECK(straddle.current == At(here));

		// Not At(here). A rollover that lost track of an already-moving placement resets this to
		// the current transform, and everything moving every frame then draws no velocity at all.
		CHECK(straddle.previous == At(before));
	}
}

// The list is what the per-frame cost scales with, so a placement written every frame must occupy
// one entry in it rather than accumulating one per frame.
TEST_CASE("A continuously moving placement is tracked once", "[transform]")
{
	auto       fixture  = Fixture();
	const auto instance = fixture.Place(0.0f);

	for (uint32_t frame = 1; frame <= 8; ++frame)
	{
		fixture.viewImpl->SetInstanceTransform(instance, At(static_cast<float>(frame)));
		fixture.viewImpl->AdvanceInstanceTransforms(frame);
	}

	CHECK(fixture.viewImpl->GetMovingInstanceCount() == 1);

	// And it leaves the list once it stops.
	fixture.viewImpl->AdvanceInstanceTransforms(9);
	CHECK(fixture.viewImpl->GetMovingInstanceCount() == 0);
}

TEST_CASE("A view drawn twice in one frame reports one history to both draws", "[transform]")
{
	auto       fixture  = Fixture();
	const auto instance = fixture.Place(0.0f);

	fixture.viewImpl->SetInstanceTransform(instance, At(1.0f));

	fixture.viewImpl->AdvanceInstanceTransforms(1);
	fixture.viewImpl->AdvanceInstanceTransforms(1);

	// The second call is the same frame, so it must not treat the first as history -- exactly the
	// rule AdvanceCamera holds for the camera.
	const auto straddle = StraddleOf(fixture.viewImpl, instance);
	CHECK(straddle.current == At(1.0f));
	CHECK(straddle.previous == At(0.0f));
}

TEST_CASE("Moving a placement does not move the temporal epoch", "[transform]")
{
	auto       fixture  = Fixture();
	const auto instance = fixture.Place(0.0f);

	// Drain whatever the placement itself raised.
	(void)fixture.viewImpl->AdvanceTemporalEpoch();
	REQUIRE_FALSE(fixture.viewImpl->AdvanceTemporalEpoch());

	fixture.viewImpl->SetInstanceTransform(instance, At(1.0f));

	// A move is described by a motion vector, so the frame after one is reprojected rather than
	// taken whole. Bumping the epoch here would leave a scene where anything moves permanently
	// unaccumulated -- see docs/taa.md.
	CHECK_FALSE(fixture.viewImpl->AdvanceTemporalEpoch());
}

TEST_CASE("A deleted placement stops being rolled", "[transform]")
{
	auto       fixture  = Fixture();
	const auto instance = fixture.Place(0.0f);

	fixture.viewImpl->SetInstanceTransform(instance, At(1.0f));
	fixture.viewImpl->DeleteMeshInstance(instance);

	// The moving list still names it. Rolling a freed slot -- or worse, whichever placement takes
	// that slot next -- is what the generation on the stored handle exists to stop.
	REQUIRE_NOTHROW(fixture.viewImpl->AdvanceInstanceTransforms(1));

	const auto reused = fixture.Place(5.0f);
	fixture.viewImpl->AdvanceInstanceTransforms(2);

	const auto straddle = StraddleOf(fixture.viewImpl, reused);
	CHECK(straddle.current == At(5.0f));
	CHECK(straddle.previous == At(5.0f));
}

TEST_CASE("The transform setter refuses a handle it does not own", "[transform]")
{
	auto       fixture  = Fixture();
	const auto instance = fixture.Place(0.0f);

	fixture.viewImpl->DeleteMeshInstance(instance);

	REQUIRE_THROWS_AS(fixture.viewImpl->SetInstanceTransform(instance, At(1.0f)), bgl::SceneError);
	REQUIRE_THROWS_AS(fixture.viewImpl->GetInstanceTransform(instance), bgl::SceneError);
	REQUIRE_THROWS_AS(
		fixture.viewImpl->SetInstanceTransform(bgl::MeshInstanceHandle(), At(1.0f)),
		bgl::SceneError);
}

TEST_CASE("GetInstanceTransform returns what was written", "[transform]")
{
	auto       fixture  = Fixture();
	const auto instance = fixture.Place(2.0f);

	CHECK(fixture.viewImpl->GetInstanceTransform(instance) == At(2.0f));

	fixture.viewImpl->SetInstanceTransform(instance, At(-4.0f));
	CHECK(fixture.viewImpl->GetInstanceTransform(instance) == At(-4.0f));
}
