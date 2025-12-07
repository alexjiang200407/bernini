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
		AsKeyEvent() noexcept;

		virtual class MouseEvent*
		AsMouseEvent() noexcept;

		virtual void
		Accept(class IWindowEventVisitor& visitor, float dt) const = 0;

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
