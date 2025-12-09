#pragma once
#include <core/EnumSet.h>
#include <core/win/IWindowEvent.h>

class GLFWwindow;

namespace core::win
{
	enum class MouseStateFlagsEnum : uint8_t
	{
		kLeftDown   = 1 << 0,
		kRightDown  = 1 << 1,
		kMiddleDown = 1 << 2,
		kButtonDown = kLeftDown | kRightDown | kMiddleDown,
	};

	using MouseStateFlags = core::EnumSet<MouseStateFlagsEnum>;

	struct MouseState
	{
		MouseStateFlags flags{};
		int             x        = 0;
		int             y        = 0;
		int             wheelPos = 0;
	};

	struct MouseDelta
	{
		int dx         = 0;
		int dy         = 0;
		int wheelDelta = 0;
	};

	class MouseEvent : public IWindowEvent
	{
	private:
		friend class IWindow;

	public:
		enum class Action
		{
			kInvalid  = 0,
			kLPress   = 1 << 0,
			kLRelease = 1 << 1,
			kLHeld    = 1 << 2,
			kRPress   = 1 << 3,
			kRRelease = 1 << 4,
			kRHeld    = 1 << 5,
			kMPress   = 1 << 6,
			kMRelease = 1 << 7,
			kMHeld    = 1 << 8,
			kWheel    = 1 << 9,
			kMove     = 1 << 10,
		};

		using Actions = core::EnumSet<Action>;

	private:
		MouseEvent() noexcept = default;
		MouseEvent(Actions actions, const MouseState& state, const MouseDelta& delta) noexcept :
			m_actions{ actions }, m_state{ state }, m_delta{ delta }
		{}

	public:
		MouseEvent*
		AsMouseEvent() noexcept override;

		void
		Accept(IWindowEventVisitor& visitor, float dt) const override;

		Actions
		GetActions() const noexcept
		{
			return m_actions;
		}

		const MouseState&
		GetState() const noexcept
		{
			return m_state;
		}

		const MouseDelta&
		GetDelta() const noexcept
		{
			return m_delta;
		}

	private:
		Actions    m_actions;
		MouseState m_state;
		MouseDelta m_delta;
	};
}
