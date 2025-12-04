#include "GfxHandle.h"
#include <Core/win/WinAPI.h>
#include <Core/win/Window.h>
#include <gfx/gfx.h>

struct BerniniGraphicseErrorChecker
{};

static const inline BerniniGraphicseErrorChecker berniniErrChecker;

namespace
{
	void
	operator>>(GfxResult berniniResult, BerniniGraphicseErrorChecker)
	{
		if (berniniResult != GFX_RESULT_OK)
		{
			auto errorInfo = getLastError();
			if (errorInfo.result == GFX_RESULT_OK)
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
		initializeGfx(LOG_LEVEL_INFO) >> berniniErrChecker;

		auto opts = core::win::WindowOptions{};

		opts.width  = 800;
		opts.height = 600;

		auto wnd = core::win::Window{ opts };

		game::GfxHandle graphics;

		createGraphics({ .wnd = { .hwnd = nullptr }, .width = 800u, .height = 600u }, &graphics) >>
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

			drawFrame(graphics) >> berniniErrChecker;
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
