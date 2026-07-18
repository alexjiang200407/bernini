#include <DemoWindow.h>
#include <bgl/bgl.h>

#include <cstdio>

// A minimal window that attaches Graphics and draws a triangle every frame -- the smallest end-to-end
// proof of a backend: create the device, open a window, present a drawable in a loop.
int
main()
{
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

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = opts.width;
		targetDesc.height   = opts.height;
		targetDesc.headless = false;
		targetDesc.wnd      = wnd.NativeHandle();

		auto target = graphics->CreateRenderTarget(targetDesc);

		while (!wnd.ShouldClose())
		{
			demo::PumpEvents();

			graphics->BeginFrame(target);
			graphics->EndFrame();
		}

		return 0;
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "fatal: %s\n", e.what());
		return 1;
	}
}
