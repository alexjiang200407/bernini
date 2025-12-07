#include <Core/win/IWindowEventVisitor.h>
#include <Core/win/KeyEvent.h>
#include <Core/win/MouseEvent.h>
#include <Core/win/Window.h>

#include <glfw/glfw3.h>
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3native.h>

namespace core::win
{
	void
	KeyEvent::GLFWCallback(GLFWwindow* wnd, int key, int sc, int action, int mods)
	{
		auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(wnd));

		auto evt    = std::unique_ptr<KeyEvent>{ new KeyEvent() };
		evt->key    = key;
		evt->sc     = sc;
		evt->action = action;
		evt->mods   = mods;

		AddToQueue(*window, std::move(evt));
	}

	bool
	KeyEvent::IsPressed() const noexcept
	{
		return action == GLFW_PRESS;
	}

	bool
	KeyEvent::IsReleased() const noexcept
	{
		return action == GLFW_RELEASE;
	}

	bool
	KeyEvent::IsRepeat() const noexcept
	{
		return action == GLFW_REPEAT;
	}

	KeyEvent*
	KeyEvent::AsKeyEvent() noexcept
	{
		return this;
	}

	void
	KeyEvent::Accept(IWindowEventVisitor& visitor, float dt) const
	{
		visitor.Visit(*this, dt);
	}

	void
	IWindowEvent::AddToQueue(Window& wnd, std::unique_ptr<IWindowEvent>&& evt)
	{
		assert(
			"Event types are invalid" && evt->GetType() > IWindowEvent::kInvalid ||
			evt->GetType() < IWindowEvent::kCount);

		wnd.queue.emplace_back(std::move(evt));
	}

}
