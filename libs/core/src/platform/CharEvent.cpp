#include <core/platform/CharEvent.h>
#include <core/platform/IPlatformEventVisitor.h>

namespace core
{
	CharEvent*
	CharEvent::AsCharEvent() noexcept
	{
		return this;
	}

	void
	CharEvent::Accept(IPlatformEventVisitor& visitor, float) const
	{
		visitor.Visit(*this);
	}
}
