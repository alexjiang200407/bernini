#include <core/platform/IPlatformEventVisitor.h>
#include <core/platform/KeyEvent.h>
#include <core/platform/MouseEvent.h>
#include <core/platform/Platform.h>

namespace core
{
	KeyEvent*
	KeyEvent::AsKeyEvent() noexcept
	{
		return this;
	}

	void
	KeyEvent::Accept(IPlatformEventVisitor& visitor, float dt) const
	{
		visitor.Visit(*this, dt);
	}

}
