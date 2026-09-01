#include "D3d12ErrorChecker.h"

namespace bgl
{
	std::wstring
	GetErrorDescription(HRESULT hr)
	{
		wchar_t*   descriptionWinalloc = nullptr;
		const auto result              = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			static_cast<DWORD>(hr),
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
	operator>>(HRESULT hr, D3d12ErrorChecker)
	{
		if (!FAILED(hr))
			return;

		logger::error("DirectX 12 Error: {}", core::str::wide_to_string(GetErrorDescription(hr)));

		std::abort();
	}
}
