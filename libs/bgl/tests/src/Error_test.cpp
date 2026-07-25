#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

// Exercises the caller-facing error contract of the exported interfaces:
// IScene throws SceneError and IGraphics throws GraphicsError when the caller
// misuses the API. Internal-logic failures (gassert/gfatal) are not exercised
// here because they intentionally terminate the process.

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

	bgl::RenderTargetDesc
	HeadlessTargetDesc()
	{
		auto desc     = bgl::RenderTargetDesc();
		desc.width    = 64;
		desc.height   = 64;
		desc.headless = true;
		return desc;
	}

	bgl::SceneDesc
	CubeSceneDesc()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 5;
		desc.initialMeshlets             = 100;
		desc.initialSubmeshes            = 5;
		desc.initialVertexBufferByteSize = 40000;
		desc.initialIndices              = 1000;
		return desc;
	}
}

TEST_CASE("SceneError on misuse", "[error][scene]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(CubeSceneDesc());
	REQUIRE(scene != nullptr);

	auto geom = scene->AddCubeGeom();
	REQUIRE(geom.IsValid());

	auto view = gfx->CreateSceneView(scene, 5);

	SECTION("Invalid material handle throws")
	{
		auto badMaterial         = bgl::MaterialHandle();
		badMaterial.materialType = bgl::MaterialType::kInvalid;
		REQUIRE_FALSE(badMaterial.IsValid());

		REQUIRE_THROWS_AS(scene->SetSubmeshMaterial(geom, 0, badMaterial), bgl::SceneError);
	}

	SECTION("Non-static-mesh geometry throws")
	{
		// A default-constructed handle is kInvalid, i.e. not kStaticMesh.
		auto badGeom = bgl::GeomHandle();

		REQUIRE_THROWS_AS(
			view->CreateStaticMeshInstance(badGeom, glm::mat4(1.0f)),
			bgl::SceneError);
	}

	SECTION("Valid arguments do not throw")
	{
		REQUIRE_NOTHROW(view->CreateStaticMeshInstance(geom, glm::mat4(1.0f)));
	}
}

TEST_CASE("Scene geometry and instance deletion", "[error][scene][delete]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(CubeSceneDesc());
	REQUIRE(scene != nullptr);

	auto geom = scene->AddCubeGeom();
	REQUIRE(geom.IsValid());

	auto view = gfx->CreateSceneView(scene, 5);

	SECTION("DeleteMeshInstance keeps the geom usable")
	{
		auto inst = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
		REQUIRE(inst.IsValid());

		REQUIRE_NOTHROW(view->DeleteMeshInstance(inst));

		// The geom itself was not removed, so it can still be instanced.
		REQUIRE_NOTHROW(view->CreateStaticMeshInstance(geom, glm::mat4(1.0f)));
	}

	SECTION("DeleteMeshInstance on an invalid handle throws")
	{
		REQUIRE_THROWS_AS(view->DeleteMeshInstance(bgl::MeshInstanceHandle{}), bgl::SceneError);
	}

	SECTION("Deleting the same instance twice throws")
	{
		auto inst = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
		REQUIRE_NOTHROW(view->DeleteMeshInstance(inst));
		REQUIRE_THROWS_AS(view->DeleteMeshInstance(inst), bgl::SceneError);
	}

	SECTION("DeleteGeom does not refuse a geom that still has live instances")
	{
		// The scene keeps no record of who placed what -- an instance copies the geom's submesh
		// range by value -- so deleting geometry out from under live instances is a caller mistake
		// the scene cannot see, not one it guards against. This pins the contract, not a guard.
		auto inst = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
		REQUIRE_NOTHROW(scene->DeleteGeom(geom));

		// Releasing the orphaned instance afterwards must still be clean: DeleteMeshInstance touches
		// only the view's own buffers and must not reach back into the geom it no longer references.
		REQUIRE_NOTHROW(view->DeleteMeshInstance(inst));
	}

	SECTION("IsGeomAlive follows the geom's lifetime")
	{
		REQUIRE(scene->IsGeomAlive(geom));

		REQUIRE_NOTHROW(scene->DeleteGeom(geom));
		REQUIRE_FALSE(scene->IsGeomAlive(geom));

		// GeomHandle::IsValid() only reports that the handle was given a geom type, so it keeps
		// answering true once the geometry is gone. IsGeomAlive is the one that consults the
		// generation, and the only way for a caller to tell.
		REQUIRE(geom.IsValid());
	}

	SECTION("IsGeomAlive is safe on a null handle")
	{
		REQUIRE_FALSE(scene->IsGeomAlive(bgl::GeomHandle{}));
	}

	SECTION("Using a deleted geom is caught as use-after-free")
	{
		REQUIRE_NOTHROW(scene->DeleteGeom(geom));

		// The handle is now stale; both reuse and a second delete are rejected.
		REQUIRE_THROWS_AS(view->CreateStaticMeshInstance(geom, glm::mat4(1.0f)), bgl::SceneError);
		REQUIRE_THROWS_AS(scene->DeleteGeom(geom), bgl::SceneError);
	}
}

TEST_CASE("Capacity is a starting point, not a limit", "[error][scene][capacity]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	SECTION("A view placed past initialInstances grows instead of throwing")
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 1;
		desc.initialMeshlets             = 8;
		desc.initialSubmeshes            = 1;
		desc.initialVertexBufferByteSize = 64 * 8 * 4;
		desc.initialIndices              = 64;

		auto scene = gfx->CreateScene(desc);
		auto view  = gfx->CreateSceneView(scene, 1);
		auto geom  = scene->AddCubeGeom();

		// A cube is six submeshes, so even the first instance overruns a one-instance view.
		for (int i = 0; i < 4; ++i)
		{
			REQUIRE_NOTHROW(view->CreateStaticMeshInstance(geom, glm::mat4(1.0f)));
		}

		CHECK(view->GetInstanceCount() > 1);
	}

	SECTION("A scene loaded past initialGeom grows instead of throwing")
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 1;
		desc.initialMeshlets             = 100;
		desc.initialSubmeshes            = 2;
		desc.initialVertexBufferByteSize = 40000;
		desc.initialIndices              = 1000;

		auto scene = gfx->CreateScene(desc);

		bgl::GeomHandle first;
		bgl::GeomHandle second;
		REQUIRE_NOTHROW(first = scene->AddCubeGeom());
		REQUIRE_NOTHROW(second = scene->AddCubeGeom());

		CHECK(scene->IsGeomAlive(first));
		CHECK(scene->IsGeomAlive(second));
	}
}

TEST_CASE("GraphicsError on frame protocol misuse", "[error][graphics]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto target = gfx->CreateRenderTarget(HeadlessTargetDesc());
	REQUIRE(target != nullptr);

	SECTION("BeginFrame while a frame is already active throws")
	{
		gfx->BeginFrame(target);
		REQUIRE_THROWS_AS(gfx->BeginFrame(target), bgl::GraphicsError);
	}

	SECTION("Draw outside of a frame throws")
	{
		auto job = bgl::RenderJob();
		job.view = nullptr;
		REQUIRE_THROWS_AS(gfx->Draw(job), bgl::GraphicsError);
	}

	SECTION("Draw with a null scene throws")
	{
		gfx->BeginFrame(target);
		auto job = bgl::RenderJob();
		job.view = nullptr;
		REQUIRE_THROWS_AS(gfx->Draw(job), bgl::GraphicsError);
	}

	SECTION("EndFrame without a matching BeginFrame throws")
	{
		REQUIRE_THROWS_AS(gfx->EndFrame(), bgl::GraphicsError);
	}

	SECTION("ScreenshotPng between BeginFrame and EndFrame throws")
	{
		gfx->BeginFrame(target);
		REQUIRE_THROWS_AS(
			gfx->ScreenshotPng(target, "assets/golden/should_not_exist.png"),
			bgl::GraphicsError);
	}
}
