#include <core/win/CharEvent.h>
#include <core/win/IWindowEventVisitor.h>

namespace core::win
{
	CharEvent*
	CharEvent::AsCharEvent() noexcept
	{
		return this;
	}

	void
	CharEvent::Accept(IWindowEventVisitor& visitor, float) const
	{
		visitor.Visit(*this);
	}
}
