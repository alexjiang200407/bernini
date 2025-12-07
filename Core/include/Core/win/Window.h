#pragma once
#include <Core/win/IWindowEvent.h>
#include <Core/win/IWindowEventVisitor.h>
#include <Core/win/KeyEvent.h>
#include <Core/win/MouseEvent.h>

#include <string>

class GLFWwindow;

namespace core::win
{
	struct WindowOptions
	{
		enum class Mode
		{
			Windowed,
			Fullscreen,
			BorderlessFullscreen
		};

		Mode             mode      = Mode::Windowed;
		int              width     = 0;
		int              height    = 0;
		std::string_view title     = "Window"sv;
		bool             visible   = true;
		bool             resizable = true;
		bool             decorated = true;
	};

	// Not thread safe
	class Window
	{
	public:
		friend class IWindowEvent;
		friend class KeyEvent;
		friend class MouseEvent;

	public:
		Window(const WindowOptions& opts = {});
		~Window() noexcept;

		bool
		PollEvents() noexcept;

		void
		Accept(class IWindowEventVisitor& visitor) noexcept;

		void
		Flush() noexcept;

		double
		GetLastMouseX() const noexcept
		{
			return lastMouseX;
		}

		double
		GetLastMouseY() const noexcept
		{
			return lastMouseY;
		}

	private:
		static constexpr size_t EVENT_TYPE_COUNT = static_cast<size_t>(IWindowEvent::kCount);

		std::vector<std::unique_ptr<IWindowEvent>> queue;
		GLFWwindow*                                glfwWindow;
		double                                     currentTime = 0.0;
		double                                     dt          = 0.0;
		double                                     lastMouseX  = 0.0;
		double                                     lastMouseY  = 0.0;
	};
}
