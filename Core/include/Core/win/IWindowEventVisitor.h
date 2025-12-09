#pragma once

namespace core::win
{
	class KeyEvent;
	class MouseEvent;
	class CharEvent;

	class IWindowEventVisitor
	{
	public:
		virtual ~IWindowEventVisitor() noexcept = default;

		virtual void
		Visit(const KeyEvent& evt, float dt)
		{
			(void)evt;
			(void)dt;
		}

		virtual void
		Visit(const MouseEvent& evt, float dt)
		{
			(void)evt;
			(void)dt;
		}

		virtual void
		Visit(const CharEvent& evt)
		{
			(void)evt;
		}
	};
}
