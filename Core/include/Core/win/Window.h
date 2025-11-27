#pragma once
#include <Core/win/IWindowEvent.h>
#include <Core/win/IWindowEventVisitor.h>
#include <Core/win/KeyEvent.h>
#include <Core/win/MouseEvent.h>

class GLFWwindow;

namespace core::win
{
	// Not thread safe
	class Window
	{
	public:
		friend class IWindowEvent;

	public:
		Window(int width = 0, int height = 0, std::string_view title = "Window"sv) noexcept;
		~Window() noexcept;

		bool
		PollEvents() const noexcept;

		void
		Accept(class IWindowEventVisitor& visitor) noexcept;

		void
		Flush() noexcept;

	private:
		using EventsRow = std::vector<std::unique_ptr<IWindowEvent>>;

		static constexpr size_t EVENT_TYPE_COUNT = static_cast<size_t>(IWindowEvent::kCount);

		std::array<EventsRow, EVENT_TYPE_COUNT> queues;
		GLFWwindow*                             glfwWindow;
	};
}
