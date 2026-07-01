#include <bgl/bgl.h>
#include <core/platform/Platform.h>
#include <format>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

struct EventVisitor : public core::IPlatformEventVisitor
{
	void
	Visit(const core::KeyEvent& e, float dt) override
	{
		(void)e;
		(void)dt;
	}

	void
	Visit(const core::MouseEvent& e, float dt) override
	{
		(void)e;
		(void)dt;
	}
};

int APIENTRY
wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	try
	{
		auto opts = core::PlatformOptions{};

		opts.width     = 800;
		opts.height    = 600;
		opts.resizable = false;
		opts.decorated = false;
		opts.mode      = core::PlatformOptions::Mode::Windowed;

		auto wnd1 = core::IPlatform::Create(opts);
		auto wnd2 = core::IPlatform::Create(opts);

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
		targetDesc.wnd      = wnd1->GetNativeHandle();

		auto targetA = gfx->CreateRenderTarget(targetDesc);

		targetDesc.wnd = wnd2->GetNativeHandle();

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

		auto sceneDesc        = bgl::SceneDesc();
		sceneDesc.maxGeom     = 8;
		sceneDesc.maxMeshlets = 512;
		sceneDesc.maxVertices = 20000;
		sceneDesc.maxIndices  = 20000;

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

		for (auto res1 = wnd1->Process(), res2 = wnd2->Process();
		     res1 != core::IPlatform::kClose || res2 != core::IPlatform::kClose;)
		{
			if (res1 != core::IPlatform::kClose)
			{
				gfx->DrawFrame(targetA, cubeContext);
				res1 = wnd1->Process();
			}

			if (res2 != core::IPlatform::kClose)
			{
				gfx->DrawFrame(targetB, twoContext);
				res2 = wnd2->Process();
			}
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
