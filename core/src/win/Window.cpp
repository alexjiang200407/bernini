#include <cassert>
#include <core/win/Window.h>

#include <core/win/KeyEvent.h>
#include <core/win/MouseEvent.h>

namespace core::win
{
	void
	IWindow::Accept(IWindowEventVisitor& visitor, float dt)
	{
		using clock = std::chrono::steady_clock;
		auto now    = clock::now();

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

	void
	IWindow::Reset() noexcept
	{
		m_mouseState = MouseState{};
		m_keysHeld.clear();
		m_queue.clear();
	}

	IWindow::WindowProcessResult
	IWindow::Process(IWindowEventVisitor* visitor)
	{
		using clock       = std::chrono::steady_clock;
		using MouseAction = MouseEvent::Action;

		auto pollEvents = PollEvents();

		if (pollEvents)
		{
			auto currentTime = clock::now();
			auto dt          = std::chrono::duration<float>(currentTime - m_lastTime).count();
			if (dt > m_processDeltaTimeSeconds)
			{
				m_lastTime = currentTime;

				if (m_mouseState.flags.all(MouseStateFlagsEnum::kLeftDown))
				{
					CreateMouseEvent(MouseAction::kLHeld, m_mouseState, {});
				}
				if (m_mouseState.flags.all(MouseStateFlagsEnum::kRightDown))
				{
					CreateMouseEvent(MouseAction::kRHeld, m_mouseState, {});
				}
				if (m_mouseState.flags.all(MouseStateFlagsEnum::kMiddleDown))
				{
					CreateMouseEvent(MouseAction::kMHeld, m_mouseState, {});
				}

				for (const auto& keyCode : m_keysHeld)
				{
					CreateKeyEvent(keyCode, KeyEvent::Type::kHeld);
				}

				if (visitor)
				{
					Accept(*visitor, dt);
				}

				Flush();

				return kProcess;
			}

			return kSkip;
		}

		return kClose;
	}

}
