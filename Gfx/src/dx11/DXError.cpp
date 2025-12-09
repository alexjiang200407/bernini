#include "dx11/DXError.h"
#include <core/str/str.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

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

namespace gfx::dx
{
	DXResult::DXResult(unsigned int hr, std::source_location loc) noexcept :
		m_hr{ hr }, m_loc{ loc }
	{}

	void
	operator>>(DXResult dxErr, DXErrorChecker)
	{
		if (!FAILED(dxErr.m_hr))
			return;

		throw DXException(std::move(dxErr));
	}

	DXException::DXException(DXResult&& result) noexcept :
		GfxException{ GFX_RESULT_ERROR_DIRECTX11_ERROR,
		              "DirectX 11 Error",
		              core::str::wide_to_string(GetErrorDescription(result.m_hr)),
		              result.m_loc },
		m_code{ result.m_hr }
	{}

}
