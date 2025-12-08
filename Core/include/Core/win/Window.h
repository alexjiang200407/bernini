#pragma once
#include <Core/win/IWindowEvent.h>
#include <Core/win/IWindowEventVisitor.h>
#include <Core/win/KeyEvent.h>
#include <Core/win/MouseEvent.h>

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
		friend class IWindowEvent;
		friend class KeyEvent;
		friend class MouseEvent;

	public:
		virtual ~IWindow() noexcept = default;

		virtual bool
		PollEvents() noexcept = 0;

		void
		Accept(class IWindowEventVisitor& visitor) noexcept;

		void
		Flush() noexcept;

		[[nodiscard]]
		static std::unique_ptr<IWindow>
		Create(const WindowOptions& options) noexcept;

	private:
		std::vector<std::unique_ptr<IWindowEvent>> queue;
	};
}
