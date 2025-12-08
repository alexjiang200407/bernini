#pragma once
#include <Core/win/IWindowEvent.h>

class GLFWwindow;

namespace core::win
{
	class MouseEvent : public IWindowEvent
	{
	private:
		friend class IWindow;

	private:
		MouseEvent() noexcept = default;

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

	private:
	};
}
