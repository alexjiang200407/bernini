#include <Core/win/IWindowEventVisitor.h>
#include <Core/win/MouseEvent.h>
#include <Core/win/Window.h>
#include <glfw/glfw3.h>

namespace core::win
{
	MouseEvent*
	MouseEvent::AsMouseEvent() noexcept
	{
		return this;
	}

	void
	MouseEvent::Accept(IWindowEventVisitor& visitor) const
	{
		visitor.Visit(*this);
	}

	void
	MouseEvent::GLFWCursorPosCallback(GLFWwindow* wnd, double xpos, double ypos)
	{
		auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(wnd));

		auto evt  = std::unique_ptr<MouseEvent>{ new MouseEvent() };
		evt->xpos = xpos;
		evt->ypos = ypos;

		evt->button = -1;
		evt->action = 0;
		evt->mods   = glfwGetKey(wnd, GLFW_KEY_LEFT_SHIFT);

		evt->leftPressed    = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
		evt->rightPressed   = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
		evt->middlePressed  = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS);
		evt->button4Pressed = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_4) == GLFW_PRESS);
		evt->button5Pressed = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_5) == GLFW_PRESS);

		AddToQueue(*window, std::move(evt));
	}

	void
	MouseEvent::GLFWMouseButtonCallback(GLFWwindow* wnd, int button, int action, int mods)
	{
		auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(wnd));

		auto evt = std::unique_ptr<MouseEvent>{ new MouseEvent() };

		double xpos = 0.0, ypos = 0.0;
		glfwGetCursorPos(wnd, &xpos, &ypos);

		evt->xpos = xpos;
		evt->ypos = ypos;

		evt->button = button;
		evt->action = action;
		evt->mods   = mods;

		evt->leftPressed    = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
		evt->rightPressed   = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
		evt->middlePressed  = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS);
		evt->button4Pressed = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_4) == GLFW_PRESS);
		evt->button5Pressed = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_5) == GLFW_PRESS);

		evt->xoffset = 0.0;
		evt->yoffset = 0.0;

		AddToQueue(*window, std::move(evt));
	}

	void
	MouseEvent::GLFWScrollCallback(GLFWwindow* wnd, double xoffset, double yoffset)
	{
		auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(wnd));

		auto evt = std::unique_ptr<MouseEvent>{ new MouseEvent() };

		double xpos = 0.0, ypos = 0.0;
		glfwGetCursorPos(wnd, &xpos, &ypos);

		evt->xpos = xpos;
		evt->ypos = ypos;

		evt->xoffset = xoffset;
		evt->yoffset = yoffset;

		evt->button = -1;
		evt->action = 0;
		evt->mods   = 0;

		evt->leftPressed    = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
		evt->rightPressed   = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
		evt->middlePressed  = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS);
		evt->button4Pressed = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_4) == GLFW_PRESS);
		evt->button5Pressed = (glfwGetMouseButton(wnd, GLFW_MOUSE_BUTTON_5) == GLFW_PRESS);

		AddToQueue(*window, std::move(evt));
	}
}
