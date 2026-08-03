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
		PumpEvents();

		unsigned int m_Id           = 0;  // SDL_WindowID
		SDL_Window*  m_Window       = nullptr;
		void*        m_NativeHandle = nullptr;  // HWND (Windows) or CAMetalLayer* (macOS)
		void*        m_MetalView    = nullptr;  // SDL_MetalView, macOS only
		bool         m_ShouldClose  = false;
	};

	void
	PumpEvents();

	inline constexpr int c_ScancodeF10 = 67;  // SDL_SCANCODE_F10

	bool
	KeyPressed(int scancode);
}
