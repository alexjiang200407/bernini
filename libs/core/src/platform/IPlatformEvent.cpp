#include <core/platform/IPlatformEvent.h>

namespace core
{
	KeyEvent*
	IPlatformEvent::AsKeyEvent() noexcept
	{
		return nullptr;
	}

	MouseEvent*
	IPlatformEvent::AsMouseEvent() noexcept
	{
		return nullptr;
	}

	CharEvent*
	IPlatformEvent::AsCharEvent() noexcept
	{
		return nullptr;
	}
}
