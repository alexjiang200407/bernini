#include <Core/win/IWindowEventVisitor.h>
#include <Core/win/MouseEvent.h>
#include <Core/win/Window.h>

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
