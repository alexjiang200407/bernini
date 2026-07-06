#include <DemoWindow.h>
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
		auto opts       = demo::WindowOptions{};
		opts.width      = 800;
		opts.height     = 600;
		opts.borderless = false;

		opts.title = "bgl_two_windows (1)";
		auto wnd1  = demo::DemoWindow{ opts };
		wnd1.SetPosition(100, 100);

		opts.title = "bgl_two_windows (2)";
		auto wnd2  = demo::DemoWindow{ opts };
		wnd2.SetPosition(100 + opts.width + 20, 100);

		auto gfxOpts                     = bgl::GraphicsOptions{};
		gfxOpts.enableDebugLayer         = true;
		gfxOpts.enableGPUValidationLayer = true;
		gfxOpts.enablePixDebug           = true;
		gfxOpts.logLevel                 = bgl::GraphicsOptions::LogLevel::kTrace;

		auto graphics = bgl::CreateGraphics(gfxOpts);

		constexpr uint32_t kWidth  = 800;
		constexpr uint32_t kHeight = 600;

		auto gfx = bgl::CreateGraphics(gfxOpts);

		// One renderer driving two independent headless outputs.
		auto targetDesc     = bgl::RenderTargetDesc();
		targetDesc.width    = static_cast<int>(kWidth);
		targetDesc.height   = static_cast<int>(kHeight);
		targetDesc.headless = false;
		targetDesc.wnd      = wnd1.NativeHandle();

		auto targetA = gfx->CreateRenderTarget(targetDesc);

		targetDesc.wnd = wnd2.NativeHandle();

		auto targetB = gfx->CreateRenderTarget(targetDesc);

		auto camera = bgl::Camera();
		auto aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		const auto viewport =
			bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

		auto sceneDesc                    = bgl::SceneDesc();
		sceneDesc.maxGeom                 = 8;
		sceneDesc.maxMeshlets             = 512;
		sceneDesc.maxSubmeshes            = 8;
		sceneDesc.maxVertexBufferByteSize = 800000;
		sceneDesc.maxIndices              = 20000;

		auto cubeScene = gfx->CreateScene(sceneDesc);
		auto cubeView  = gfx->CreateSceneView(cubeScene, 8);
		auto cubeGeomA = cubeScene->AddCubeGeom();
		cubeView->CreateStaticMeshInstance(cubeGeomA, glm::mat4(1.0f));

		auto cubeContext     = bgl::RenderContext();
		cubeContext.view     = cubeView;
		cubeContext.camera   = camera;
		cubeContext.viewport = viewport;

		// Target B: two cubes (origin + x=-5), matching the two_cubes golden.
		auto twoScene  = gfx->CreateScene(sceneDesc);
		auto twoView   = gfx->CreateSceneView(twoScene, 8);
		auto cubeGeomB = twoScene->AddCubeGeom();

		auto secondTransform  = glm::mat4(1.0f);
		secondTransform[3][0] = -5.0f;
		twoView->CreateStaticMeshInstance(cubeGeomB, glm::mat4(1.0f));
		twoView->CreateStaticMeshInstance(cubeGeomB, secondTransform);

		auto twoContext     = bgl::RenderContext();
		twoContext.view     = twoView;
		twoContext.camera   = camera;
		twoContext.viewport = viewport;

		while (!wnd1.ShouldClose() || !wnd2.ShouldClose())
		{
			demo::PumpEvents();

			if (!wnd1.ShouldClose())
			{
				gfx->DrawFrame(targetA, cubeContext);
			}

			if (!wnd2.ShouldClose())
			{
				gfx->DrawFrame(targetB, twoContext);
			}
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
