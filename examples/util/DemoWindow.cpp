#include "DemoWindow.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_metal.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace demo
{
	namespace
	{
		std::vector<DemoWindow*>&
		Registry() noexcept
		{
			static std::vector<DemoWindow*> g_Windows;
			return g_Windows;
		}

		std::set<int>&
		PressedKeys() noexcept
		{
			static std::set<int> g_Pressed;
			return g_Pressed;
		}
	}

	DemoWindow::DemoWindow(const WindowOptions& options)
	{
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		{
			throw std::runtime_error(std::string("SDL_InitSubSystem failed: ") + SDL_GetError());
		}

		SDL_WindowFlags flags = 0;
		if (options.borderless)
			flags |= SDL_WINDOW_BORDERLESS;
		if (options.resizable)
			flags |= SDL_WINDOW_RESIZABLE;

		m_Window = SDL_CreateWindow(options.title, options.width, options.height, flags);
		if (!m_Window)
		{
			std::string err = SDL_GetError();
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			throw std::runtime_error("SDL_CreateWindow failed: " + err);
		}

		m_Id = SDL_GetWindowID(m_Window);
#if defined(__APPLE__)
		// A Metal target binds to a CAMetalLayer, not a raw window handle. SDL owns the view and
		// its layer; the backend interprets `RenderTargetDesc::wnd` as the CAMetalLayer.
		m_MetalView    = SDL_Metal_CreateView(m_Window);
		m_NativeHandle = SDL_Metal_GetLayer(m_MetalView);
#else
		m_NativeHandle = SDL_GetPointerProperty(
			SDL_GetWindowProperties(m_Window),
			SDL_PROP_WINDOW_WIN32_HWND_POINTER,
			nullptr);
#endif

		if (options.captureMouse)
		{
			// Relative mode hides and grabs the cursor and reports raw motion deltas.
			SDL_SetWindowRelativeMouseMode(m_Window, true);
		}

		Registry().push_back(this);
	}

	DemoWindow::~DemoWindow()
	{
		auto& reg = Registry();
		reg.erase(std::remove(reg.begin(), reg.end(), this), reg.end());

#if defined(__APPLE__)
		if (m_MetalView)
			SDL_Metal_DestroyView(m_MetalView);
#endif

		if (m_Window)
			SDL_DestroyWindow(m_Window);

		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}

	void
	DemoWindow::SetPosition(int x, int y) noexcept
	{
		SDL_SetWindowPosition(m_Window, x, y);
	}

	void
	PumpEvents(const std::function<void(const SDL_Event&)>& sink)
	{
		PressedKeys().clear();

		SDL_Event e;
		while (SDL_PollEvent(&e))
		{
			// Before the window's own handling, so a sink sees a close request too.
			if (sink)
			{
				sink(e);
			}

			switch (e.type)
			{
			case SDL_EVENT_QUIT:
				for (auto* w : Registry()) w->m_ShouldClose = true;
				break;

			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				for (auto* w : Registry())
				{
					if (w->m_Id == e.window.windowID)
						w->m_ShouldClose = true;
				}
				break;

			case SDL_EVENT_KEY_DOWN:
				// Ignore auto-repeat so a held key fires KeyPressed() only once.
				if (!e.key.repeat)
					PressedKeys().insert(static_cast<int>(e.key.scancode));
				break;

			default:
				break;
			}
		}
	}

	bool
	KeyPressed(int scancode)
	{
		return PressedKeys().contains(scancode);
	}
}
