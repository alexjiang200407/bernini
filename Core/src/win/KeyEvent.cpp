#include <core/win/IWindowEventVisitor.h>
#include <core/win/KeyEvent.h>
#include <core/win/MouseEvent.h>
#include <core/win/Window.h>

namespace core::win
{
	KeyEvent*
	KeyEvent::AsKeyEvent() noexcept
	{
		return this;
	}

	void
	KeyEvent::Accept(IWindowEventVisitor& visitor, float dt) const
	{
		visitor.Visit(*this, dt);
	}

}
