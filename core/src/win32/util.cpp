#include "win32/util.h"
#include <core/str/str.h>

namespace core::win::win32
{
	std::wstring
	getErrorDescription(DWORD dw)
	{
		wchar_t*   descriptionWinalloc = nullptr;
		const auto result              = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			dw,
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

	void
	operator>>(HRESULT hr, Win32ErrorChecker)
	{
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Win32 API Error: " +
				core::str::wide_to_string(getErrorDescription(static_cast<DWORD>(hr))));
		}
	}

}
