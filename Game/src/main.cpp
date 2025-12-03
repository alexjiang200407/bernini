#include "GfxHandle.h"
#include <Core/win/WinAPI.h>
#include <Core/win/Window.h>
#include <gfx/gfx.h>

struct BerniniRenderereErrorChecker
{};

static const inline BerniniRenderereErrorChecker berniniErrChecker;

namespace
{
	void
	operator>>(Bernini_GfxResult berniniResult, BerniniRenderereErrorChecker)
	{
		if (berniniResult != BERNINI_GFX_RENDERER_RESULT_OK)
		{
			auto errorInfo = bernini_getLastError();
			if (errorInfo.result == BERNINI_GFX_RENDERER_RESULT_OK)
			{
				errorInfo = { .result  = berniniResult,
					          .title   = "Unknown Error",
					          .message = "An unknown error has occurred." };
			}
			throw std::runtime_error{ std::format("{}: {}", errorInfo.title, errorInfo.message) };
		}
	}
}

int APIENTRY
wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	try
	{
		auto opts = core::win::WindowOptions{};

		opts.width  = 800;
		opts.height = 600;

		auto wnd = core::win::Window{ opts };

		game::GfxHandle renderer;

		bernini_createRenderer(
			{ .wnd = { .hwnd = nullptr }, .width = 800u, .height = 600u },
			&renderer) >>
			berniniErrChecker;

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

			bernini_drawFrame(renderer) >> berniniErrChecker;
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
