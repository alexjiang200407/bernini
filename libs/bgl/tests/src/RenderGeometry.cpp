#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "gfx/GraphicsBase.h"
#include "resource/ResourceManager.h"
#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include <bgl/IGraphics.h>

TEST_CASE("Geometry", "[geometry][render]")
{
	constexpr uint32_t kWidth  = 600;
	constexpr uint32_t kHeight = 800;

	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.enablePixDebug           = true;
	auto gfx                      = bgl::CreateGraphics(opts);

	REQUIRE(gfx != nullptr);
	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);
	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(kWidth);
	targetDesc.height   = static_cast<int>(kHeight);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto device       = gfxBase->GetDevice();
	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	auto camera = bgl::Camera();
	auto aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 8;
	sceneDesc.maxMeshlets             = 512;
	sceneDesc.maxSubmeshes            = 8;
	sceneDesc.maxVertexBufferByteSize = 800000;
	sceneDesc.maxIndices              = 20000;

	SECTION("Draw Cube - cube.png")
	{
		auto scene = gfxBase->CreateScene(sceneDesc);
		auto view  = gfxBase->CreateSceneView(scene, 8);

		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto cubeGeom = scene->AddCubeGeom();
		auto cubeInst = view->CreateStaticMeshInstance(cubeGeom, glm::mat4(1.0f));

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

		gfxBase->DrawFrame(target, context);
		gfxBase->ScreenshotRaw(target, "assets/golden/cube.got.png");

		// Compare against the committed golden (deployed next to the exe under
		// assets/golden/); on mismatch (or a missing golden) "assets/golden/cube.got.png" is left
		// behind for inspection.
		CHECK(bgl::test::MatchesGolden("assets/golden/cube.exp.png", "assets/golden/cube.got.png"));
	}

	SECTION("Draw Plane - plane.png")
	{
		auto scene = gfxBase->CreateScene(sceneDesc);
		auto view  = gfxBase->CreateSceneView(scene, 8);

		// Head-on, the same camera the cube uses: the quad faces +Z, so this looks straight at it and
		// it should render as a bounded square, not a surface running off to a horizon.
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		// Deliberately not square: a width/height swap would otherwise be invisible here.
		auto planeGeom = scene->AddPlaneGeom(4, 4, 12.0f, 6.0f);
		view->CreateStaticMeshInstance(planeGeom, glm::mat4(1.0f));

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

		gfxBase->DrawFrame(target, context);
		gfxBase->ScreenshotRaw(target, "assets/golden/plane.got.png");

		CHECK(
			bgl::test::MatchesGolden("assets/golden/plane.exp.png", "assets/golden/plane.got.png"));
	}

	SECTION("A subdivided plane splits into meshlets and still draws")
	{
		// 64 x 64 quads is 8192 triangles -- far past the 124-triangle / 64-vertex cap a single
		// meshlet can hold, so unlike the flat 1x1 plane this actually exercises the meshlet split.
		auto denseDesc                    = sceneDesc;
		denseDesc.maxMeshlets             = 1024;
		denseDesc.maxSubmeshes            = 8;
		denseDesc.maxVertexBufferByteSize = 4u * 1024u * 1024u;
		denseDesc.maxIndices              = 200000;

		auto scene = gfxBase->CreateScene(denseDesc);
		auto view  = gfxBase->CreateSceneView(scene, 8);

		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		bgl::GeomHandle planeGeom;
		REQUIRE_NOTHROW(planeGeom = scene->AddPlaneGeom(64, 64, 12.0f, 6.0f));
		REQUIRE_NOTHROW(view->CreateStaticMeshInstance(planeGeom, glm::mat4(1.0f)));

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

		REQUIRE_NOTHROW(gfxBase->DrawFrame(target, context));

		// A subdivided plane is the same surface as a flat one, so it must land on the same pixels --
		// which is what catches a meshlet split that drops or duplicates a triangle.
		gfxBase->ScreenshotRaw(target, "assets/golden/plane_dense.got.png");
		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/plane.exp.png",
				"assets/golden/plane_dense.got.png"));
	}

	SECTION("A rotated plane is the floor - plane_floor.png")
	{
		// The plane carries no orientation of its own; a floor is just the quad rotated onto XZ. That
		// is the whole argument for it being an upright quad, so it is worth pinning down.
		auto scene = gfxBase->CreateScene(sceneDesc);
		auto view  = gfxBase->CreateSceneView(scene, 8);

		// Above the floor, looking down at it.
		camera
			.LookAt(
				glm::vec3(0.0f, 6.0f, 12.0f),
				glm::vec3(0.0f, 0.0f, 0.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		// -90 degrees about X takes +Z (the quad's normal) to +Y (up).
		const auto toFloor =
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

		auto planeGeom = scene->AddPlaneGeom(4, 4, 10.0f, 10.0f);
		view->CreateStaticMeshInstance(planeGeom, toFloor);

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

		gfxBase->DrawFrame(target, context);
		gfxBase->ScreenshotRaw(target, "assets/golden/plane_floor.got.png");

		// Visible from above rather than back-face culled to an empty frame: the rotation took the
		// front face with it, so the winding is right.
		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/plane_floor.exp.png",
				"assets/golden/plane_floor.got.png"));
	}

	SECTION("AddPlaneGeom rejects a zero segment count")
	{
		// A zero would divide by zero deriving the UVs, and there is no sensible plane to build.
		auto scene = gfxBase->CreateScene(sceneDesc);

		CHECK_THROWS_AS(scene->AddPlaneGeom(0, 4, 1.0f, 1.0f), bgl::SceneError);
		CHECK_THROWS_AS(scene->AddPlaneGeom(4, 0, 1.0f, 1.0f), bgl::SceneError);
	}

	SECTION("Draw Sphere and Cube - sphere_cube.png")
	{
		auto scene = gfxBase->CreateScene(sceneDesc);
		auto view  = gfxBase->CreateSceneView(scene, 8);

		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto cubeGeom   = scene->AddCubeGeom();
		auto sphereGeom = scene->AddSphereGeom(32, 32, 1.0f);

		auto cubeTransform = glm::mat4(1.0f);

		auto sphereTransform  = glm::mat4(1.0f);
		sphereTransform[3][0] = -5.0f;

		view->CreateStaticMeshInstance(cubeGeom, cubeTransform);
		view->CreateStaticMeshInstance(sphereGeom, sphereTransform);

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

		gfxBase->DrawFrame(target, context);
		gfxBase->ScreenshotRaw(target, "assets/golden/sphere_cube.got.png");

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/sphere_cube.exp.png",
				"assets/golden/sphere_cube.got.png"));
	}

	SECTION("Two scenes in one frame (cube + sphere) - sphere_cube.png")
	{
		auto cubeScene   = gfxBase->CreateScene(sceneDesc);
		auto sphereScene = gfxBase->CreateScene(sceneDesc);

		auto cubeView   = gfxBase->CreateSceneView(cubeScene, 8);
		auto sphereView = gfxBase->CreateSceneView(sphereScene, 8);

		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto cubeGeom = cubeScene->AddCubeGeom();
		cubeView->CreateStaticMeshInstance(cubeGeom, glm::mat4(1.0f));

		auto sphereGeom       = sphereScene->AddSphereGeom(32, 32, 1.0f);
		auto sphereTransform  = glm::mat4(1.0f);
		sphereTransform[3][0] = -5.0f;
		sphereView->CreateStaticMeshInstance(sphereGeom, sphereTransform);

		const auto viewport =
			bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

		auto cubeContext     = bgl::RenderContext();
		cubeContext.view     = cubeView;
		cubeContext.camera   = camera;
		cubeContext.viewport = viewport;

		auto sphereContext     = bgl::RenderContext();
		sphereContext.view     = sphereView;
		sphereContext.camera   = camera;
		sphereContext.viewport = viewport;

		gfxBase->BeginFrame(target);
		gfxBase->Draw(cubeContext);
		gfxBase->Draw(sphereContext);
		gfxBase->EndFrame();

		gfxBase->ScreenshotRaw(target, "assets/golden/two_scenes_sphere_cube.got.png");

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/sphere_cube.exp.png",
				"assets/golden/two_scenes_sphere_cube.got.png"));
	}

	SECTION("Draw Two Cubes - two_cubes.png")
	{
		auto scene = gfxBase->CreateScene(sceneDesc);
		auto view  = gfxBase->CreateSceneView(scene, 8);

		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		// Same transforms as the sphere/cube case, but both meshes are cubes.
		auto cubeGeom = scene->AddCubeGeom();

		auto firstTransform = glm::mat4(1.0f);

		auto secondTransform  = glm::mat4(1.0f);
		secondTransform[3][0] = -5.0f;

		view->CreateStaticMeshInstance(cubeGeom, firstTransform);
		view->CreateStaticMeshInstance(cubeGeom, secondTransform);

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

		gfxBase->DrawFrame(target, context);
		gfxBase->ScreenshotRaw(target, "assets/golden/two_cubes.got.png");

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/two_cubes.exp.png",
				"assets/golden/two_cubes.got.png"));
	}

	SECTION("Delete with Sphere and Cube - cube.png")
	{
		auto scene = gfxBase->CreateScene(sceneDesc);
		auto view  = gfxBase->CreateSceneView(scene, 8);

		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto cubeGeom   = scene->AddCubeGeom();
		auto sphereGeom = scene->AddSphereGeom(32, 32, 1.0f);

		auto sphereTransform  = glm::mat4(1.0f);
		sphereTransform[3][0] = -5.0f;

		view->CreateStaticMeshInstance(cubeGeom, glm::mat4(1.0f));
		auto sphereInst = view->CreateStaticMeshInstance(sphereGeom, sphereTransform);

		// Removing the sphere instance leaves only the cube at the origin, so the
		// frame must match the lone-cube golden.
		view->DeleteMeshInstance(sphereInst);

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

		gfxBase->DrawFrame(target, context);
		gfxBase->ScreenshotRaw(target, "assets/golden/delete_sphere_cube.got.png");

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/cube.exp.png",
				"assets/golden/delete_sphere_cube.got.png"));
	}

	SECTION("Delete with 2 cubes - cube.png")
	{
		auto scene = gfxBase->CreateScene(sceneDesc);
		auto view  = gfxBase->CreateSceneView(scene, 8);

		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto cubeGeom = scene->AddCubeGeom();

		auto secondTransform  = glm::mat4(1.0f);
		secondTransform[3][0] = -5.0f;

		view->CreateStaticMeshInstance(cubeGeom, glm::mat4(1.0f));
		auto secondInst = view->CreateStaticMeshInstance(cubeGeom, secondTransform);

		// Removing the x=-5 cube leaves only the cube at the origin.
		view->DeleteMeshInstance(secondInst);

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

		gfxBase->DrawFrame(target, context);
		gfxBase->ScreenshotRaw(target, "assets/golden/delete_two_cubes.got.png");

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/cube.exp.png",
				"assets/golden/delete_two_cubes.got.png"));
	}
}

// Proves one Graphics (renderer/device) can drive multiple independent outputs:
// a cube goes to target A and two cubes go to target B in separate frames, then each
// target is read back and compared to its own golden.
TEST_CASE("Render to two targets", "[geometry][render][multitarget]")
{
	constexpr uint32_t kWidth  = 600;
	constexpr uint32_t kHeight = 800;

	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.enablePixDebug           = true;
	auto gfx                      = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	// One renderer driving two independent headless outputs.
	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(kWidth);
	targetDesc.height   = static_cast<int>(kHeight);
	targetDesc.headless = true;

	auto targetA = gfx->CreateRenderTarget(targetDesc);
	auto targetB = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(targetA != nullptr);
	REQUIRE(targetB != nullptr);

	auto camera = bgl::Camera();
	auto aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 20.0f),
			glm::vec3(0.0f, 0.0f, 19.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

	const auto viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 8;
	sceneDesc.maxMeshlets             = 512;
	sceneDesc.maxSubmeshes            = 8;
	sceneDesc.maxVertexBufferByteSize = 800000;
	sceneDesc.maxIndices              = 20000;

	// Target A: a single cube at the origin (matches the lone-cube golden).
	auto cubeScene = gfxBase->CreateScene(sceneDesc);
	auto cubeView  = gfxBase->CreateSceneView(cubeScene, 8);
	auto cubeGeomA = cubeScene->AddCubeGeom();
	cubeView->CreateStaticMeshInstance(cubeGeomA, glm::mat4(1.0f));

	auto cubeContext     = bgl::RenderContext();
	cubeContext.view     = cubeView;
	cubeContext.camera   = camera;
	cubeContext.viewport = viewport;

	// Target B: two cubes (origin + x=-5), matching the two_cubes golden.
	auto twoScene  = gfxBase->CreateScene(sceneDesc);
	auto twoView   = gfxBase->CreateSceneView(twoScene, 8);
	auto cubeGeomB = twoScene->AddCubeGeom();

	auto secondTransform  = glm::mat4(1.0f);
	secondTransform[3][0] = -5.0f;
	twoView->CreateStaticMeshInstance(cubeGeomB, glm::mat4(1.0f));
	twoView->CreateStaticMeshInstance(cubeGeomB, secondTransform);

	auto twoContext     = bgl::RenderContext();
	twoContext.view     = twoView;
	twoContext.camera   = camera;
	twoContext.viewport = viewport;

	// Independent frames against each output from the one renderer.
	gfxBase->DrawFrame(targetA, cubeContext);
	gfxBase->DrawFrame(targetB, twoContext);

	gfxBase->ScreenshotRaw(targetA, "assets/golden/two_targets_a.got.png");
	gfxBase->ScreenshotRaw(targetB, "assets/golden/two_targets_b.got.png");

	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/cube.exp.png",
			"assets/golden/two_targets_a.got.png"));
	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/two_cubes.exp.png",
			"assets/golden/two_targets_b.got.png"));
}
