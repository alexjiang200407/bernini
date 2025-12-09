#pragma once
#include <core/win/IWindowEvent.h>
#include <core/win/IWindowEventVisitor.h>
#include <core/win/KeyEvent.h>
#include <core/win/MouseEvent.h>

#include <string>

namespace core::win
{
	struct WindowOptions
	{
		enum class Mode
		{
			Windowed,
			Fullscreen,
			BorderlessWindowed
		};

		Mode             mode      = Mode::Windowed;
		int              width     = 0;
		int              height    = 0;
		std::string_view title     = "Window"sv;
		bool             visible   = true;
		bool             resizable = true;
		bool             decorated = true;
	};

	// Not thread safe
	class IWindow
	{
	public:
		enum WindowProcessResult
		{
			kContinue,
			kSkip,
			kClose
		};

	public:
		IWindow(const WindowOptions& options) noexcept :
			m_width{ options.width }, m_height{ options.height }, m_windowMode{ options.mode }
		{
			using clock = std::chrono::steady_clock;
			m_lastTime  = clock::now();
		}

		virtual ~IWindow() noexcept = default;

		void
		Accept(class IWindowEventVisitor& visitor) noexcept;

		void
		Flush() noexcept;

		[[nodiscard]]
		static std::unique_ptr<IWindow>
		Create(const WindowOptions& options) noexcept;

		WindowProcessResult
		Process() noexcept;

	protected:
		void
		CreateMouseEvent(
			MouseEvent::Actions actions,
			const MouseState&   state,
			const MouseDelta&   delta) noexcept
		{
			m_queue.emplace_back(new MouseEvent(actions, state, delta));
		}

	private:
		virtual bool
		PollEvents() noexcept = 0;

	protected:
		std::vector<std::unique_ptr<IWindowEvent>> m_queue;
		MouseState                                 m_mouseState{};
		WindowOptions::Mode                        m_windowMode;
		int                                        m_width;
		int                                        m_height;
		std::chrono::steady_clock::time_point      m_lastTime;
		static constexpr float                     UPDATE_DELTA_TIME_MS = 0.05f;
	};
}
