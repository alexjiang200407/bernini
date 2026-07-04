#include <core/platform/IPlatformEventVisitor.h>
#include <core/platform/MouseEvent.h>
#include <core/platform/Platform.h>

namespace core
{
	MouseEvent*
	MouseEvent::AsMouseEvent() noexcept
	{
		return this;
	}

	void
	MouseEvent::Accept(IPlatformEventVisitor& visitor, float dt) const
	{
		visitor.Visit(*this, dt);
	}

}
