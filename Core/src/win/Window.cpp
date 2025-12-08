#include <Core/win/Window.h>
#include <cassert>

#include <Core/except/BerniniException.h>
#include <Core/win/KeyEvent.h>
#include <Core/win/MouseEvent.h>

namespace core::win
{
	void
	IWindow::Accept(IWindowEventVisitor& visitor) noexcept
	{
		for (auto& evt : queue)
		{
			evt->Accept(visitor, 0.0f);
		}
	}

	void
	IWindow::Flush() noexcept
	{
		queue.clear();
	}

}
