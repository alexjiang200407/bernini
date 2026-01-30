#pragma once
#include <core/win/CharEvent.h>
#include <core/win/IWindowEvent.h>
#include <core/win/IWindowEventVisitor.h>
#include <core/win/KeyEvent.h>
#include <core/win/MouseEvent.h>

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

		Mode             mode                    = Mode::Windowed;
		int              width                   = 0;
		int              height                  = 0;
		std::string_view title                   = "Window"sv;
		bool             visible                 = true;
		bool             resizable               = true;
		bool             decorated               = true;
		float            processDeltaTimeSeconds = 0.005f;
	};

	// Not thread safe
	class IWindow
	{
	public:
		enum WindowProcessResult
		{
			kProcess,
			kSkip,
			kClose
		};

	public:
		IWindow(const WindowOptions& options) noexcept :
			m_width{ options.width }, m_height{ options.height }, m_windowMode{ options.mode },
			m_processDeltaTimeSeconds{ options.processDeltaTimeSeconds }
		{
			using clock = std::chrono::steady_clock;
			m_lastTime  = clock::now();
		}

		virtual ~IWindow() noexcept = default;

		void
		Flush() noexcept;

		void
		Reset() noexcept;

		[[nodiscard]]
		static std::unique_ptr<IWindow>
		Create(const WindowOptions& options);

		WindowProcessResult
		Process(IWindowEventVisitor* visitor = nullptr);

	protected:
		void
		CreateMouseEvent(
			MouseEvent::Actions actions,
			const MouseState&   state,
			const MouseDelta&   delta)
		{
			m_queue.emplace_back(new MouseEvent(actions, state, delta));
		}

		void
		CreateKeyEvent(unsigned int keyCode, KeyEvent::Type type)
		{
			if (type == KeyEvent::Type::kPress)
			{
				m_keysHeld.insert(keyCode);
			}
			else if (type == KeyEvent::Type::kRelease)
			{
				m_keysHeld.erase(keyCode);
			}
			m_queue.emplace_back(new KeyEvent(keyCode, type));
		}

		void
		CreateCharEvent(char32_t ch)
		{
			m_queue.emplace_back(new CharEvent(ch));
		}

	private:
		virtual bool
		PollEvents() noexcept = 0;

		void
		Accept(class IWindowEventVisitor& visitor, float dt);

	protected:
		std::vector<std::unique_ptr<IWindowEvent>> m_queue;
		MouseState                                 m_mouseState{};
		std::unordered_set<unsigned int>           m_keysHeld;
		WindowOptions::Mode                        m_windowMode;
		int                                        m_width;
		int                                        m_height;
		std::chrono::steady_clock::time_point      m_lastTime;

		// Since we don't want to process events each time we poll,
		// we check that this amount of time has passed before
		float m_processDeltaTimeSeconds = 0.005f;
	};
}
