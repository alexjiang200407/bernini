#include "win32/WinAPI.h"
#include <core/file/file.h>

namespace core::file
{
	std::filesystem::path
	get_library_path()
	{
		HMODULE module = nullptr;

		BOOL ok = GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&get_library_path),
			&module);

		if (!ok || module == nullptr)
			return {};

		wchar_t buffer[MAX_PATH];
		DWORD   len = GetModuleFileNameW(module, buffer, MAX_PATH);
		if (len == 0)
			return {};

		return std::filesystem::path{ buffer };
	}

	std::filesystem::path
	get_executable_path()
	{
		wchar_t buffer[MAX_PATH];
		DWORD   len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (len == 0)
			return {};

		return std::filesystem::path{ buffer };
	}
}
