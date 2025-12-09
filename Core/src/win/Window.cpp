#include <cassert>
#include <core/win/Window.h>

#include <core/except/BerniniException.h>
#include <core/win/KeyEvent.h>
#include <core/win/MouseEvent.h>

namespace core::win
{
	void
	IWindow::Accept(IWindowEventVisitor& visitor) noexcept
	{
		using clock = std::chrono::steady_clock;
		auto now    = clock::now();
		auto dt     = std::chrono::duration<float>(now - m_lastTime).count();

		for (auto& evt : m_queue)
		{
			evt->Accept(visitor, dt);
		}
	}

	void
	IWindow::Flush() noexcept
	{
		m_queue.clear();
	}

	IWindow::WindowProcessResult
	IWindow::Process() noexcept
	{
		using clock       = std::chrono::steady_clock;
		using MouseAction = MouseEvent::Action;

		auto pollEvents = PollEvents();

		if (pollEvents)
		{
			auto currentTime = clock::now();
			auto dt          = std::chrono::duration<float>(currentTime - m_lastTime).count();
			if (dt > UPDATE_DELTA_TIME_MS)
			{
				m_lastTime = currentTime;

				if (m_mouseState.flags.All(MouseStateFlagsEnum::kLeftDown))
				{
					CreateMouseEvent(MouseAction::kLHeld, m_mouseState, {});
				}
				if (m_mouseState.flags.All(MouseStateFlagsEnum::kRightDown))
				{
					CreateMouseEvent(MouseAction::kRHeld, m_mouseState, {});
				}
				if (m_mouseState.flags.All(MouseStateFlagsEnum::kMiddleDown))
				{
					CreateMouseEvent(MouseAction::kMHeld, m_mouseState, {});
				}
				return kContinue;
			}

			return kSkip;
		}

		return kClose;
	}

}
