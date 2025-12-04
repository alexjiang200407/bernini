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

		unsigned int         hr;
		std::source_location loc;
	};

	class DXException : public GfxException
	{
	public:
		DXException(DXResult&& result) noexcept;

		unsigned int
		GetHR() const noexcept
		{
			return code;
		}

	private:
		unsigned int code;
	};

	struct DXErrorChecker
	{};

	static const inline DXErrorChecker dxErrorChecker;

	void
	operator>>(DXResult dxErrChk, DXErrorChecker);

}
