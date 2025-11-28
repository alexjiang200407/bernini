#pragma once
#include <Renderer/Renderer.h>

namespace renderer
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

	class DXException : public RendererException
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

	struct DX12ErrorChecker
	{};

	static const inline DX12ErrorChecker dxErrorChecker;

	void
	operator>>(DXResult dxErrChk, DX12ErrorChecker);

}
