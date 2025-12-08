#include <Core/win/IWindowEventVisitor.h>
#include <Core/win/KeyEvent.h>
#include <Core/win/MouseEvent.h>
#include <Core/win/Window.h>

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

	void
	IWindowEvent::AddToQueue(IWindow& wnd, std::unique_ptr<IWindowEvent>&& evt)
	{
		assert(
			"Event types are invalid" && evt->GetType() > IWindowEvent::kInvalid ||
			evt->GetType() < IWindowEvent::kCount);

		wnd.queue.emplace_back(std::move(evt));
	}

}
