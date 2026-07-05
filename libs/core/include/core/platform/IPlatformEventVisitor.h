#pragma once

namespace core
{
	class KeyEvent;
	class MouseEvent;
	class CharEvent;

	class IPlatformEventVisitor
	{
	public:
		virtual ~IPlatformEventVisitor() noexcept = default;

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
