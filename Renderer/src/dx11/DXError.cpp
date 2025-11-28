#include "dx11/DXError.h"
#include <Core/str/str.h>
#include <Core/win/WinAPI.h>

namespace
{
	std::wstring
	GetErrorDescription(HRESULT hr)
	{
		wchar_t*   descriptionWinalloc = nullptr;
		const auto result              = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            hr,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&descriptionWinalloc),
            0,
            nullptr);

		std::wstring description;
		if (result)
		{
			description = descriptionWinalloc;
			LocalFree(descriptionWinalloc);
			if (description.ends_with(L"\r\n"))
			{
				description.resize(description.size() - 2);
			}
		}
		return description;
	}
}

namespace renderer
{
	DXResult::DXResult(unsigned int hr, std::source_location loc) noexcept : hr{ hr }, loc{ loc } {}

	void
	operator>>(DXResult dxErr, DX12ErrorChecker)
	{
		if (!FAILED(dxErr.hr))
			return;

		throw DXException(std::move(dxErr));
	}

	DXException::DXException(DXResult&& result) noexcept :
		RendererException{ core::str::wide_to_string(GetErrorDescription(result.hr)), result.loc },
		code{ result.hr }
	{}

}
