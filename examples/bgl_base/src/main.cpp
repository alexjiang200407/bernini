#include <bgl/bgl.h>
#include <core/win/Window.h>
#include <format>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

struct EventVisitor : public core::win::IWindowEventVisitor
{
	void
	Visit(const core::win::KeyEvent& e, float dt) override
	{
		(void)e;
		(void)dt;
	}

	void
	Visit(const core::win::MouseEvent& e, float dt) override
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
		auto opts = core::win::WindowOptions{};

		opts.width     = 800;
		opts.height    = 600;
		opts.resizable = false;
		opts.decorated = false;
		opts.mode      = core::win::WindowOptions::Mode::BorderlessWindowed;

		auto wnd = core::win::IWindow::Create(opts);

		auto gfxOpts                     = bgl::GraphicsOptions{};
		gfxOpts.width                    = opts.width;
		gfxOpts.height                   = opts.height;
		gfxOpts.headless                 = false;
		gfxOpts.enableDebugLayer         = true;
		gfxOpts.enableGPUValidationLayer = true;
		gfxOpts.enablePixDebug           = true;

		auto graphics = bgl::Graphics(gfxOpts);

		auto visitor = EventVisitor{};

		for (auto res = wnd->Process(&visitor); res != core::win::IWindow::kClose;
		     res      = wnd->Process(&visitor))
		{
			graphics.DrawFrame();
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
