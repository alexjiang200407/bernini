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

	public:
		Window(const WindowOptions& opts = {}) noexcept;
		~Window() noexcept;

		bool
		PollEvents() const noexcept;

		void
		Accept(class IWindowEventVisitor& visitor) noexcept;

		void
		Flush() noexcept;

	private:
		static constexpr size_t EVENT_TYPE_COUNT = static_cast<size_t>(IWindowEvent::kCount);

		std::vector<std::unique_ptr<IWindowEvent>> queue;
		GLFWwindow*                                glfwWindow;
	};
}
