#include <CLI/CLI.hpp>
#include <DemoWindow.h>
#include <FlyCamera.h>
#include <assetlib/image_io.h>
#include <bgl/bgl.h>
#include <format>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

int APIENTRY
wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	try
	{
		uint32_t width         = 800;
		uint32_t height        = 600;
		float    roughness     = 0.5f;
		float    metallic      = 0.5f;
		float    sphereRadius  = 2.0f;
		bool     skyBoxEnabled = true;

		{
			CLI::App app{ "Bernini bgl_base example" };
			app.set_help_flag("--help", "Print this help message and exit");
			app.add_option("-w,--width", width, "Window width in pixels")
				->check(CLI::PositiveNumber);
			app.add_option("-h,--height", height, "Window height in pixels")
				->check(CLI::PositiveNumber);
			app.add_option("-r,--roughness", roughness, "Roughness")->check(CLI::NonNegativeNumber);
			app.add_option("--radius", sphereRadius, "Sphere Radius")->check(CLI::PositiveNumber);
			app.add_option("-m,--metallic", metallic, "Metallic")->check(CLI::NonNegativeNumber);
			app.add_option("-s,--skybox", skyBoxEnabled, "Enable skybox rendering");

			CLI11_PARSE(app, __argc, __wargv);
		}

		auto opts         = demo::WindowOptions{};
		opts.width        = static_cast<int>(width);
		opts.height       = static_cast<int>(height);
		opts.title        = "Bernini bgl_sphere";
		opts.borderless   = true;
		opts.captureMouse = true;

		auto wnd = demo::DemoWindow{ opts };

		auto gfxOpts                     = bgl::GraphicsOptions{};
		gfxOpts.enableDebugLayer         = true;
		gfxOpts.enableGPUValidationLayer = false;
		gfxOpts.enablePixDebug           = true;
		gfxOpts.logLevel                 = bgl::GraphicsOptions::LogLevel::kTrace;

		auto graphics = bgl::CreateGraphics(gfxOpts);

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = static_cast<int>(width);
		targetDesc.height   = static_cast<int>(height);
		targetDesc.headless = false;
		targetDesc.wnd      = wnd.NativeHandle();
		auto target         = graphics->CreateRenderTarget(targetDesc);

		auto sceneDesc                    = bgl::SceneDesc();
		sceneDesc.maxIndices              = 10000;
		sceneDesc.maxVertexBufferByteSize = 100000;
		sceneDesc.maxGeom                 = 100;
		sceneDesc.maxMeshlets             = 1000;
		sceneDesc.maxSubmeshes            = 100;
		sceneDesc.maxPbrMaterials         = 100;

		auto scene = graphics->CreateScene(std::move(sceneDesc));
		auto view  = graphics->CreateSceneView(scene, 100);
		auto pmrem = scene->AddTextureAsset(assetlib::loadDDS("assets/pmrem.dds"));

		view->SetEnvironmentMap(
			{ scene->AddTextureAsset(assetlib::loadDDS("assets/iem.dds")),
		      pmrem,
		      scene->AddTextureAsset(assetlib::loadDDS("assets/brdf_lut.dds")) });

		if (skyBoxEnabled)
		{
			view->SetSkyBox({ pmrem });
		}

		auto metalMat = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(1.0f),
		      .metallicFactor  = metallic,
		      .roughnessFactor = roughness });

		auto cube   = scene->AddCubeGeom(metalMat);
		auto sphere = scene->AddSphereGeom(32, 32, sphereRadius, metalMat);

		auto transform = glm::mat4(1.0f);

		auto inst2 = view->CreateStaticMeshInstance(sphere, transform);

		const float aspect = static_cast<float>(width) / static_cast<float>(height);

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto context     = bgl::RenderContext{};
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(width), static_cast<float>(height));

		// WASD to fly, hold Shift + move the mouse to look around.
		auto clock = demo::DeltaClock{};
		while (!wnd.ShouldClose())
		{
			demo::PumpEvents();

			if (demo::ApplyFlyCam(camera, clock.Tick()))
			{
				// context holds a copy of the camera; refresh it after moving.
				context.camera = camera;
			}

			graphics->DrawFrame(target, context);
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
