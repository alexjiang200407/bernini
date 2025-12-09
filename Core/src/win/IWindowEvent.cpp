#include <core/win/IWindowEvent.h>

namespace core::win
{
	KeyEvent*
	IWindowEvent::AsKeyEvent() noexcept
	{
		return nullptr;
	}

	MouseEvent*
	IWindowEvent::AsMouseEvent() noexcept
	{
		return nullptr;
	}
}
