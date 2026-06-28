#pragma once
#include <core/platform/IPlatformEvent.h>

namespace core
{
	class KeyEvent : public IPlatformEvent
	{
	public:
		enum Type
		{
			kPress,
			kRelease,
			kHeld,
			kInvalid
		};

	private:
		friend class IPlatform;

	private:
		KeyEvent() noexcept = default;
		KeyEvent(unsigned int keyCode, Type type) noexcept : m_keyCode{ keyCode }, m_type{ type } {}

	public:
		KeyEvent*
		AsKeyEvent() noexcept override;

		void
		Accept(class IPlatformEventVisitor& visitor, float dt) const override;

		bool
		IsReleased() const noexcept
		{
			return m_type == kRelease;
		}

		bool
		IsPress() const noexcept
		{
			return m_type == kPress;
		}

		bool
		IsHeld() const noexcept
		{
			return m_type == kHeld;
		}

		unsigned int
		GetKeyCode() const noexcept
		{
			return m_keyCode;
		}

	private:
		unsigned int m_keyCode = 0u;
		Type         m_type    = kInvalid;
	};
}
