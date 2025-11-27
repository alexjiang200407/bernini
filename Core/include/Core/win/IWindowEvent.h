#pragma once

namespace core::win
{
	class IWindowEvent
	{
	public:
		enum Type
		{
			kInvalid = -1,
			kKey,
			kMouseButton,
			kCancel,
			kCount,
		};

	public:
		virtual ~IWindowEvent() noexcept = default;

		virtual class KeyEvent*
		AsKeyEvent() noexcept = 0;

		virtual class MouseEvent*
		AsMouseEvent() noexcept = 0;

		virtual void
		Accept(class IWindowEventVisitor& visitor) const = 0;

		static constexpr Type
		GetTypeStatic() noexcept
		{
			return kInvalid;
		}

		virtual Type
		GetType() const noexcept
		{
			return GetTypeStatic();
		}

	protected:
		static void
		AddToQueue(class Window& wnd, std::unique_ptr<IWindowEvent>&& evt);
	};
}