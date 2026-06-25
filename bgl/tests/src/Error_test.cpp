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
		opts.headless         = true;
		opts.width            = 64;
		opts.height           = 64;
		opts.wnd              = nullptr;
		opts.enableDebugLayer = false;
		return opts;
	}

	bgl::SceneDesc
	CubeSceneDesc()
	{
		auto desc         = bgl::SceneDesc();
		desc.maxInstances = 5;
		desc.maxGeom      = 5;
		desc.maxMeshlets  = 100;
		desc.maxVertices  = 1000;
		desc.maxIndices   = 1000;
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

	SECTION("Invalid material handle throws")
	{
		auto badMaterial         = bgl::MaterialHandle();
		badMaterial.materialType = bgl::MaterialType::kInvalid;
		REQUIRE_FALSE(badMaterial.IsValid());

		REQUIRE_THROWS_AS(
			scene->CreateStaticMeshInstance(geom, badMaterial, glm::mat4(1.0f)),
			bgl::SceneError);
	}

	SECTION("Non-static-mesh geometry throws")
	{
		auto validMaterial         = bgl::MaterialHandle();
		validMaterial.materialType = bgl::MaterialType::kPBR;
		REQUIRE(validMaterial.IsValid());

		// A default-constructed handle is kInvalid, i.e. not kStaticMesh.
		auto badGeom = bgl::GeomHandle();

		REQUIRE_THROWS_AS(
			scene->CreateStaticMeshInstance(badGeom, validMaterial, glm::mat4(1.0f)),
			bgl::SceneError);
	}

	SECTION("Valid arguments do not throw")
	{
		auto material         = bgl::MaterialHandle();
		material.materialType = bgl::MaterialType::kPBR;

		REQUIRE_NOTHROW(scene->CreateStaticMeshInstance(geom, material, glm::mat4(1.0f)));
	}
}

TEST_CASE("GraphicsError on frame protocol misuse", "[error][graphics]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	SECTION("BeginFrame while a frame is already active throws")
	{
		gfx->BeginFrame();
		REQUIRE_THROWS_AS(gfx->BeginFrame(), bgl::GraphicsError);
	}

	SECTION("Draw outside of a frame throws")
	{
		auto context  = bgl::RenderContext();
		context.scene = nullptr;
		REQUIRE_THROWS_AS(gfx->Draw(context), bgl::GraphicsError);
	}

	SECTION("Draw with a null scene throws")
	{
		gfx->BeginFrame();
		auto context  = bgl::RenderContext();
		context.scene = nullptr;
		REQUIRE_THROWS_AS(gfx->Draw(context), bgl::GraphicsError);
	}

	SECTION("EndFrame without a matching BeginFrame throws")
	{
		REQUIRE_THROWS_AS(gfx->EndFrame(), bgl::GraphicsError);
	}

	SECTION("ScreenshotRaw between BeginFrame and EndFrame throws")
	{
		gfx->BeginFrame();
		REQUIRE_THROWS_AS(gfx->ScreenshotRaw("golden/should_not_exist.dds"), bgl::GraphicsError);
	}
}
