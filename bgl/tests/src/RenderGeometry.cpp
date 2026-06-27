#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "gfx/GraphicsBase.h"
#include "resource/ResourceManager.h"
#include "util/GoldenImage.h"
#include <bgl/IGraphics.h>

TEST_CASE("Geometry", "[geometry][render]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = true;
	opts.headless                 = true;
	opts.height                   = 800;
	opts.width                    = 600;
	opts.wnd                      = nullptr;
	opts.enablePixDebug           = true;
	auto gfx                      = bgl::CreateGraphics(opts);

	REQUIRE(gfx != nullptr);
	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);
	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device       = gfxBase->GetDevice();
	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	auto camera = bgl::Camera();
	auto aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);

	auto sceneDesc         = bgl::SceneDesc();
	sceneDesc.maxInstances = 8;
	sceneDesc.maxGeom      = 8;
	sceneDesc.maxMeshlets  = 512;
	sceneDesc.maxVertices  = 20000;
	sceneDesc.maxIndices   = 20000;

	SECTION("Draw Cube - cube.dds")
	{
		auto scene = gfxBase->CreateScene(sceneDesc);

		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto cubeGeom = scene->AddCubeGeom();
		auto cubeInst = scene->CreateStaticMeshInstance(cubeGeom, glm::mat4(1.0f));

		auto context   = bgl::RenderContext();
		context.scene  = scene;
		context.camera = camera;
		context.viewport =
			bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

		gfxBase->DrawFrame(context);
		gfxBase->ScreenshotRaw("golden/cube.got.dds");

		// Compare against the committed golden (deployed next to the exe under
		// golden/); on mismatch (or a missing golden) "golden/cube.got.dds" is left
		// behind for inspection.
		CHECK(bgl::test::MatchesGoldenDDS("golden/cube.exp.dds", "golden/cube.got.dds"));
	}

	SECTION("Draw Sphere and Cube - sphere_cube.dds")
	{
		auto scene = gfxBase->CreateScene(sceneDesc);

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

		scene->CreateStaticMeshInstance(cubeGeom, {}, cubeTransform);
		scene->CreateStaticMeshInstance(sphereGeom, {}, sphereTransform);

		auto context   = bgl::RenderContext();
		context.scene  = scene;
		context.camera = camera;
		context.viewport =
			bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

		gfxBase->DrawFrame(context);
		gfxBase->ScreenshotRaw("golden/sphere_cube.got.dds");

		CHECK(
			bgl::test::MatchesGoldenDDS(
				"golden/sphere_cube.exp.dds",
				"golden/sphere_cube.got.dds"));
	}

	SECTION("Two scenes in one frame (cube + sphere) - sphere_cube.dds")
	{
		auto cubeScene   = gfxBase->CreateScene(sceneDesc);
		auto sphereScene = gfxBase->CreateScene(sceneDesc);

		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto cubeGeom = cubeScene->AddCubeGeom();
		cubeScene->CreateStaticMeshInstance(cubeGeom, glm::mat4(1.0f));

		auto sphereGeom       = sphereScene->AddSphereGeom(32, 32, 1.0f);
		auto sphereTransform  = glm::mat4(1.0f);
		sphereTransform[3][0] = -5.0f;
		sphereScene->CreateStaticMeshInstance(sphereGeom, sphereTransform);

		const auto viewport =
			bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

		auto cubeContext     = bgl::RenderContext();
		cubeContext.scene    = cubeScene;
		cubeContext.camera   = camera;
		cubeContext.viewport = viewport;

		auto sphereContext     = bgl::RenderContext();
		sphereContext.scene    = sphereScene;
		sphereContext.camera   = camera;
		sphereContext.viewport = viewport;

		gfxBase->BeginFrame();
		gfxBase->Draw(cubeContext);
		gfxBase->Draw(sphereContext);
		gfxBase->EndFrame();

		gfxBase->ScreenshotRaw("golden/two_scenes_sphere_cube.got.dds");

		CHECK(
			bgl::test::MatchesGoldenDDS(
				"golden/sphere_cube.exp.dds",
				"golden/two_scenes_sphere_cube.got.dds"));
	}

	SECTION("Draw Two Cubes - two_cubes.dds")
	{
		auto scene = gfxBase->CreateScene(sceneDesc);

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

		scene->CreateStaticMeshInstance(cubeGeom, firstTransform);
		scene->CreateStaticMeshInstance(cubeGeom, secondTransform);

		auto context   = bgl::RenderContext();
		context.scene  = scene;
		context.camera = camera;
		context.viewport =
			bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

		gfxBase->DrawFrame(context);
		gfxBase->ScreenshotRaw("golden/two_cubes.got.dds");

		CHECK(bgl::test::MatchesGoldenDDS("golden/two_cubes.exp.dds", "golden/two_cubes.got.dds"));
	}

	SECTION("Delete with Sphere and Cube - cube.dds")
	{
		auto scene = gfxBase->CreateScene(sceneDesc);

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

		scene->CreateStaticMeshInstance(cubeGeom, glm::mat4(1.0f));
		auto sphereInst = scene->CreateStaticMeshInstance(sphereGeom, sphereTransform);

		// Removing the sphere instance leaves only the cube at the origin, so the
		// frame must match the lone-cube golden.
		scene->DeleteMeshInstance(sphereInst);

		auto context   = bgl::RenderContext();
		context.scene  = scene;
		context.camera = camera;
		context.viewport =
			bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

		gfxBase->DrawFrame(context);
		gfxBase->ScreenshotRaw("golden/delete_sphere_cube.got.dds");

		CHECK(
			bgl::test::MatchesGoldenDDS(
				"golden/cube.exp.dds",
				"golden/delete_sphere_cube.got.dds"));
	}

	SECTION("Delete with 2 cubes - cube.dds")
	{
		auto scene = gfxBase->CreateScene(sceneDesc);

		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto cubeGeom = scene->AddCubeGeom();

		auto secondTransform  = glm::mat4(1.0f);
		secondTransform[3][0] = -5.0f;

		scene->CreateStaticMeshInstance(cubeGeom, glm::mat4(1.0f));
		auto secondInst = scene->CreateStaticMeshInstance(cubeGeom, secondTransform);

		// Removing the x=-5 cube leaves only the cube at the origin.
		scene->DeleteMeshInstance(secondInst);

		auto context   = bgl::RenderContext();
		context.scene  = scene;
		context.camera = camera;
		context.viewport =
			bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

		gfxBase->DrawFrame(context);
		gfxBase->ScreenshotRaw("golden/delete_two_cubes.got.dds");

		CHECK(
			bgl::test::MatchesGoldenDDS("golden/cube.exp.dds", "golden/delete_two_cubes.got.dds"));
	}
}
