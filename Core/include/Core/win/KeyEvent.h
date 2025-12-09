#pragma once
#include <Core/win/IWindowEvent.h>
#include <Core/win/Key.h>

class GLFWwindow;

namespace core::win
{
	class KeyEvent : public IWindowEvent
	{
	private:
		friend class IWindow;

	private:
		KeyEvent() noexcept = default;

	public:
		KeyEvent*
		AsKeyEvent() noexcept override;

		void
		Accept(class IWindowEventVisitor& visitor, float dt) const override;

	private:
	};
}
