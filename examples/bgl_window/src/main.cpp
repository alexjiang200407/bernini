#include <DemoWindow.h>
#include <SDL3/SDL.h>
#include <bgl/bgl.h>
#include <core/err/util.h>

// A minimal window that attaches Graphics and draws a triangle every frame -- the smallest end-to-end
// proof of a backend: create the device, open a window, present a drawable in a loop.
int
main()
{
	core::install_crash_handlers();

	try
	{
		auto opts       = demo::WindowOptions{};
		opts.width      = 960;
		opts.height     = 600;
		opts.title      = "Bernini bgl_window";
		opts.borderless = false;

		auto wnd = demo::DemoWindow{ opts };

		auto gfxOpts     = bgl::GraphicsOptions{};
		gfxOpts.logLevel = bgl::GraphicsOptions::LogLevel::kInfo;

		auto graphics = bgl::CreateGraphics(gfxOpts);
		auto ctx      = graphics->CreateRenderContext();

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = opts.width;
		targetDesc.height   = opts.height;
		targetDesc.headless = false;
		targetDesc.wnd      = wnd.NativeHandle();

		auto target = ctx->CreateRenderTarget(targetDesc);

		while (!wnd.ShouldClose())
		{
			demo::PumpEvents();

			ctx->BeginFrame(target);
			ctx->EndFrame();
		}

		return 0;
	}
	catch (const std::exception& e)
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Bernini - Fatal Error", e.what(), nullptr);
		return 1;
	}
}
