#include "DemoWindow.h"

#include <SDL3/SDL.h>

namespace demo
{
	namespace
	{
		std::vector<DemoWindow*>&
		Registry() noexcept
		{
			static std::vector<DemoWindow*> s_windows;
			return s_windows;
		}

		std::set<int>&
		PressedKeys() noexcept
		{
			static std::set<int> s_pressed;
			return s_pressed;
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

		m_window = SDL_CreateWindow(options.title, options.width, options.height, flags);
		if (!m_window)
		{
			std::string err = SDL_GetError();
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			throw std::runtime_error("SDL_CreateWindow failed: " + err);
		}

		m_id           = SDL_GetWindowID(m_window);
		m_nativeHandle = SDL_GetPointerProperty(
			SDL_GetWindowProperties(m_window),
			SDL_PROP_WINDOW_WIN32_HWND_POINTER,
			nullptr);

		if (options.captureMouse)
		{
			// Relative mode hides and grabs the cursor and reports raw motion deltas.
			SDL_SetWindowRelativeMouseMode(m_window, true);
		}

		Registry().push_back(this);
	}

	DemoWindow::~DemoWindow()
	{
		auto& reg = Registry();
		reg.erase(std::remove(reg.begin(), reg.end(), this), reg.end());

		if (m_window)
			SDL_DestroyWindow(m_window);

		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}

	void
	DemoWindow::SetPosition(int x, int y) noexcept
	{
		SDL_SetWindowPosition(m_window, x, y);
	}

	void
	PumpEvents()
	{
		PressedKeys().clear();

		SDL_Event e;
		while (SDL_PollEvent(&e))
		{
			switch (e.type)
			{
			case SDL_EVENT_QUIT:
				for (auto* w : Registry()) w->m_shouldClose = true;
				break;

			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				for (auto* w : Registry())
				{
					if (w->m_id == e.window.windowID)
						w->m_shouldClose = true;
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
