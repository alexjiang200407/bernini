#pragma once
#include <Core/win/IWindowEvent.h>

class GLFWwindow;

namespace core::win
{
	class MouseEvent : public IWindowEvent
	{
	private:
		friend class Window;

	private:
		MouseEvent() noexcept = default;

		static void
		GLFWCursorPosCallback(GLFWwindow* wnd, double xpos, double ypos);

		static void
		GLFWMouseButtonCallback(GLFWwindow* wnd, int button, int action, int mods);

		static void
		GLFWScrollCallback(GLFWwindow* wnd, double xoffset, double yoffset);

	public:
		KeyEvent*
		AsKeyEvent() noexcept override;

		MouseEvent*
		AsMouseEvent() noexcept override;

		void
		Accept(IWindowEventVisitor& visitor) const override;

		static constexpr Type
		GetTypeStatic() noexcept
		{
			return kMouseButton;
		}

		Type
		GetType() const noexcept override
		{
			return GetTypeStatic();
		}

	private:
		double xpos    = 0.0;
		double ypos    = 0.0;
		double xoffset = 0.0;
		double yoffset = 0.0;
		int    button  = -1;
		int    action  = 0;
		int    mods    = 0;

		bool leftPressed    = false;
		bool rightPressed   = false;
		bool middlePressed  = false;
		bool button4Pressed = false;
		bool button5Pressed = false;
	};
}
