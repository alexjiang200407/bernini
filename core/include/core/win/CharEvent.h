#pragma once
#include <core/win/IWindowEvent.h>

namespace core::win
{
	class CharEvent : public IWindowEvent
	{
	private:
		friend class IWindow;

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
		Accept(IWindowEventVisitor& visitor, float dt) const override;

	private:
		char32_t m_character;
	};
}
