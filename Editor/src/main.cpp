#include <Core/win/WinAPI.h>
#include <Core/win/Window.h>
#include <Renderer/Renderer.h>

int APIENTRY
wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	try
	{
		auto opts = core::win::WindowOptions{};

		opts.width  = 800;
		opts.height = 600;

		auto wnd      = core::win::Window{ opts };
		auto renderer = renderer::Renderer{};

		while (wnd.PollEvents())
		{
			class EventVisitor : public core::win::IWindowEventVisitor
			{
				void
				Visit(const core::win::KeyEvent&) override
				{
					OutputDebugString("Key event received\n");
				}

				void
				Visit(const core::win::MouseEvent&) override
				{
					OutputDebugString("Mouse event received\n");
				}
			};

			auto visitor = EventVisitor{};
			wnd.Accept(visitor);
			wnd.Flush();

			renderer.DrawFrame();
		}
	}
	catch (const renderer::RendererException& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Renderer Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
