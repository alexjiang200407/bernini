#pragma once
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <functional>

namespace demo
{
	struct WindowOptions
	{
		int         width        = 800;
		int         height       = 600;
		const char* title        = "Bernini";
		bool        borderless   = true;
		bool        resizable    = false;
		bool        captureMouse = false;
	};

	class DemoWindow
	{
	public:
		explicit DemoWindow(const WindowOptions& options);
		~DemoWindow();

		DemoWindow(const DemoWindow&) = delete;
		DemoWindow&
		operator=(const DemoWindow&) = delete;

		void*
		NativeHandle() const noexcept
		{
			return m_NativeHandle;
		}

		bool
		ShouldClose() const noexcept
		{
			return m_ShouldClose;
		}

		void
		SetPosition(int x, int y) noexcept;

	private:
		friend void
		PumpEvents(const std::function<void(const SDL_Event&)>& sink);

		unsigned int m_Id           = 0;  // SDL_WindowID
		SDL_Window*  m_Window       = nullptr;
		void*        m_NativeHandle = nullptr;  // HWND (Windows) or CAMetalLayer* (macOS)
		void*        m_MetalView    = nullptr;  // SDL_MetalView, macOS only
		bool         m_ShouldClose  = false;
	};

	/**
	 * Drains SDL's queue, handling quit, close and key-down as it always has, and handing every
	 * event to `sink` first.
	 *
	 * One pump: two would race for the same queue and each would see half the events. A UI that
	 * wants the mouse passes a sink that forwards to it -- see RmlInput.h.
	 */
	void
	PumpEvents(const std::function<void(const SDL_Event&)>& sink = {});

	inline constexpr int c_ScancodeF10 = 67;  // SDL_SCANCODE_F10

	bool
	KeyPressed(int scancode);
}
