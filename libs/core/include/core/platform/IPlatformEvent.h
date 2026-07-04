#pragma once

namespace core
{
	class IPlatformEvent
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
		virtual ~IPlatformEvent() noexcept = default;

		virtual class KeyEvent*
		AsKeyEvent() noexcept;

		virtual class MouseEvent*
		AsMouseEvent() noexcept;

		virtual class CharEvent*
		AsCharEvent() noexcept;

		virtual void
		Accept(class IPlatformEventVisitor& visitor, float dt) const = 0;
	};
}
