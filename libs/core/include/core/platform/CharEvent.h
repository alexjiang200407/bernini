#pragma once
#include <core/platform/IPlatformEvent.h>

namespace core
{
	class CharEvent : public IPlatformEvent
	{
	private:
		friend class IPlatform;

	private:
		CharEvent() noexcept = default;
		CharEvent(char32_t character) noexcept : m_character{ character } {}

	public:
		char32_t
		GetChar() const noexcept
		{
			return m_character;
		}

		CharEvent*
		AsCharEvent() noexcept override;

		void
		Accept(IPlatformEventVisitor& visitor, float dt) const override;

	private:
		char32_t m_character;
	};
}
