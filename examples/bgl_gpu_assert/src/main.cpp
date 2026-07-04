#include <bgl/bgl.h>
#include <core/platform/Platform.h>

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
		auto opts      = core::PlatformOptions{};
		opts.width     = 800;
		opts.height    = 600;
		opts.resizable = false;
		opts.decorated = true;
		opts.mode      = core::PlatformOptions::Mode::BorderlessWindowed;

		auto wnd = core::IPlatform::Create(opts);

		auto gfxOpts                     = bgl::GraphicsOptions{};
		gfxOpts.enableDebugLayer         = true;
		gfxOpts.enableGPUValidationLayer = false;
		gfxOpts.enablePixDebug           = true;
		gfxOpts.logLevel                 = bgl::GraphicsOptions::LogLevel::kTrace;

		auto graphics = bgl::CreateGraphics(gfxOpts);

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = opts.width;
		targetDesc.height   = opts.height;
		targetDesc.headless = false;
		targetDesc.wnd      = wnd->GetNativeHandle();
		auto target         = graphics->CreateRenderTarget(targetDesc);

		auto sceneDesc                    = bgl::SceneDesc();
		sceneDesc.maxIndices              = 10000;
		sceneDesc.maxVertexBufferByteSize = 400000;
		sceneDesc.maxGeom                 = 100;
		sceneDesc.maxMeshlets             = 1000;
		sceneDesc.maxSubmeshes            = 100;

		auto scene = graphics->CreateScene(std::move(sceneDesc));
		auto view  = graphics->CreateSceneView(scene, 100);
		auto cube  = scene->AddCubeGeom();

		auto assertMaterial = bgl::MaterialHandle(bgl::MaterialType::kAssert, core::slot_handle());
		view->CreateStaticMeshInstance(cube, assertMaterial, glm::mat4(1.0f));

		const float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto context   = bgl::RenderContext{};
		context.view   = view;
		context.camera = camera;
		context.viewport =
			bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));

		auto visitor = EventVisitor();
		for (auto res = wnd->Process(&visitor); res != core::IPlatform::kClose;
		     res      = wnd->Process(&visitor))
		{
			// With BGL_PIXEL_ASSERT_DEMO enabled, the assertion fired here is read back
			// and crashes a couple of frames later inside a later DrawFrame/BeginFrame.
			graphics->DrawFrame(target, context);
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
