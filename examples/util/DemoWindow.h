#pragma once

struct SDL_Window;

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
			return m_nativeHandle;
		}

		bool
		ShouldClose() const noexcept
		{
			return m_shouldClose;
		}

		void
		SetPosition(int x, int y) noexcept;

	private:
		friend void
		PumpEvents();

		unsigned int m_id           = 0;  // SDL_WindowID
		SDL_Window*  m_window       = nullptr;
		void*        m_nativeHandle = nullptr;
		bool         m_shouldClose  = false;
	};

	void
	PumpEvents();
}
