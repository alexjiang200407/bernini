#include <core/win/IWindowEventVisitor.h>
#include <core/win/MouseEvent.h>
#include <core/win/Window.h>

namespace core::win
{
	MouseEvent*
	MouseEvent::AsMouseEvent() noexcept
	{
		return this;
	}

	void
	MouseEvent::Accept(IWindowEventVisitor& visitor, float dt) const
	{
		visitor.Visit(*this, dt);
	}

}
