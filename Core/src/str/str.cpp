#include <Core/str/str.h>

#ifdef _WIN32
#	include <Core/win/WinAPI.h>
#endif

std::wstring
core::str::string_to_wide(std::string_view str)
{
#ifndef _WIN32
#	error \
		"core::str::string_to_wide is only supported on Windows. Provide a platform-specific implementation."
#endif

	const size_t cSize = str.size() + 1;

	std::wstring wc;
	wc.resize(cSize);

	size_t cSize1;
	mbstowcs_s(&cSize1, wc.data(), cSize, str.data(), cSize);

	wc.pop_back();
	return wc;
}

std::string
core::str::wide_to_string(std::wstring_view ws)
{
#ifndef _WIN32
#	error \
		"core::str::wide_to_string is only supported on Windows. Provide a platform-specific implementation."
#endif

	if (ws.empty())
		return {};

	int size_needed =
		WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);

	std::string result(size_needed, 0);

	WideCharToMultiByte(
		CP_UTF8,
		0,
		ws.data(),
		(int)ws.size(),
		result.data(),
		size_needed,
		nullptr,
		nullptr);

	return result;
}
