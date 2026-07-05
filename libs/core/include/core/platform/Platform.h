#pragma once
#include <core/platform/CharEvent.h>
#include <core/platform/IPlatformEvent.h>
#include <core/platform/IPlatformEventVisitor.h>
#include <core/platform/KeyEvent.h>
#include <core/platform/MouseEvent.h>

namespace core
{
	struct PlatformOptions
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

	union PlatformNativeHandle
	{
		uint64_t id;
		void*    ptr;
	};

	// Not thread safe
	class IPlatform
	{
	public:
		enum PlatformProcessResult
		{
			kProcess,
			kSkip,
			kClose
		};

	public:
		IPlatform(const PlatformOptions& options) noexcept :
			m_width{ options.width }, m_height{ options.height }, m_platformMode{ options.mode },
			m_processDeltaTimeSeconds{ options.processDeltaTimeSeconds }
		{
			using clock = std::chrono::steady_clock;
			m_lastTime  = clock::now();
		}

		virtual ~IPlatform() noexcept = default;

		virtual void*
		GetNativeHandle() const noexcept = 0;

		void
		Flush() noexcept;

		void
		Reset() noexcept;

		[[nodiscard]]
		static std::unique_ptr<IPlatform>
		Create(const PlatformOptions& options);

		PlatformProcessResult
		Process(IPlatformEventVisitor* visitor = nullptr);

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
		Accept(class IPlatformEventVisitor& visitor, float dt);

	protected:
		int                                          m_width;
		int                                          m_height;
		PlatformOptions::Mode                        m_platformMode;
		float                                        m_processDeltaTimeSeconds = 0.005f;
		std::vector<std::unique_ptr<IPlatformEvent>> m_queue;
		MouseState                                   m_mouseState{};
		std::unordered_set<unsigned int>             m_keysHeld;
		std::chrono::steady_clock::time_point        m_lastTime;

		// Since we don't want to process events each time we poll,
		// we check that this amount of time has passed before
	};
}
