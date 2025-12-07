#pragma once
#include <Core/win/IWindowEvent.h>
#include <Core/win/Key.h>

class GLFWwindow;

namespace core::win
{
	class KeyEvent : public IWindowEvent
	{
	private:
		friend class Window;

	private:
		KeyEvent() noexcept = default;

		static void
		GLFWCallback(GLFWwindow* wnd, int key, int sc, int action, int mods);

	public:
		KeyEvent*
		AsKeyEvent() noexcept override;

		void
		Accept(class IWindowEventVisitor& visitor, float dt) const override;

		static constexpr Type
		GetTypeStatic() noexcept
		{
			return kKey;
		}

		Type
		GetType() const noexcept override
		{
			return GetTypeStatic();
		}

		bool
		IsPressed() const noexcept;

		bool
		IsReleased() const noexcept;

		bool
		IsRepeat() const noexcept;

		KeyCode
		GetKey() const noexcept
		{
			return static_cast<KeyCode>(key);
		}

	private:
		int key    = 0;
		int sc     = 0;
		int action = 0;
		int mods   = 0;
	};
}
