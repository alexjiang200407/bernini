#include <Core/except/BerniniException.h>
#include <Core/file/file.h>

#if defined(_WIN32)
#	include <Core/win/WinAPI.h>
#else
#	include <dlfcn.h>
#endif

namespace core::file
{
	std::vector<std::byte>
	readFileBytes(const std::string& filePath)
	{
		std::ifstream fileStream{ filePath, std::ios::binary | std::ios::ate };
		if (!fileStream)
		{
			throw core::except::BerniniException{ "Open File Error",
				                                  "Failed to open file: " + filePath };
		}
		std::streamsize fileSize = fileStream.tellg();
		fileStream.seekg(0, std::ios::beg);
		std::vector<std::byte> buffer(fileSize);
		if (!fileStream.read(reinterpret_cast<char*>(buffer.data()), fileSize))
		{
			throw core::except::BerniniException{ "Open File Error",
				                                  "Failed to read file: " + filePath };
		}
		return buffer;
	}

	std::filesystem::path
	getLibraryPath()
	{
#if defined(_WIN32)
		HMODULE module = nullptr;

		BOOL ok = GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&getLibraryPath),
			&module);

		if (!ok || module == nullptr)
			return {};

		wchar_t buffer[MAX_PATH];
		DWORD   len = GetModuleFileNameW(module, buffer, MAX_PATH);
		if (len == 0)
			return {};

		return std::filesystem::path{ buffer };
#else
		Dl_info info{};
		if (dladdr(reinterpret_cast<void*>(&getLibraryPath), &info) == 0)
			return {};

		return fs::canonical(info.dli_fname);
#endif
	}
}
