#pragma once

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

	struct DXErrorChecker
	{};

	static const inline DXErrorChecker dxErrorChecker;

	void
	operator>>(DXResult dxErrChk, DXErrorChecker);

}
