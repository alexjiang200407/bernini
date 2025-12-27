#pragma once
#include "GfxException.h"

namespace gfx::dx
{
	// Result returned from a DirectX function call
	struct DXResult
	{
		DXResult(
			unsigned int         hr,
			std::source_location loc = std::source_location::current()) noexcept;

		unsigned int         m_hr;
		std::source_location m_loc;
	};

	class DXException : public GfxException
	{
	public:
		DXException(DXResult&& result) noexcept;

		unsigned int
		GetHR() const noexcept
		{
			return m_code;
		}

	private:
		unsigned int m_code;
	};

	struct DXErrorChecker
	{};

	static const inline DXErrorChecker dxErrorChecker;

	void
	operator>>(DXResult dxErrChk, DXErrorChecker);

}
