#pragma once

namespace core::win
{
	class KeyEvent;
	class MouseEvent;

	class IWindowEventVisitor
	{
	public:
		virtual ~IWindowEventVisitor() noexcept = default;

		virtual void
		Visit(const KeyEvent& evt) = 0;

		virtual void
		Visit(const MouseEvent& evt) = 0;
	};
}