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
		MouseEvent*
		AsMouseEvent() noexcept override;

		void
		Accept(IWindowEventVisitor& visitor, float dt) const override;

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

		double
		GetDeltaX() const noexcept
		{
			return dx;
		}

		double
		GetDeltaY() const noexcept
		{
			return dy;
		}

	private:
		double xpos    = 0.0;
		double ypos    = 0.0;
		double scrollX = 0.0;
		double scrollY = 0.0;
		double dx      = 0.0;
		double dy      = 0.0;
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
