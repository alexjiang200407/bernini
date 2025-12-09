#include "WinAPI.h"
#include <concepts>
#include <core/except/BerniniException.h>
#include <core/str/str.h>

namespace core::win::win32
{
	template <typename Fn>
		requires std::invocable<Fn> && std::same_as<std::invoke_result_t<Fn>, LRESULT>
	LRESULT
	win32Invoke(Fn&& fn)
	{
		try
		{
			SetLastError(NO_ERROR);
			return std::invoke(std::forward<Fn>(fn));
		}
		catch (const core::except::BerniniException& e)
		{
			MessageBoxA(NULL, e.Body().data(), e.Title().data(), MB_ICONERROR | MB_OK);
			PostQuitMessage(-1);
		}
		catch (const std::exception& e)
		{
			MessageBoxA(NULL, e.what(), "C++ standard exception", MB_ICONERROR | MB_OK);
			PostQuitMessage(-1);
		}
		catch (...)
		{
			MessageBoxA(
				NULL,
				"Unknown Fatal Error has occurred in Window",
				"Fatal Error",
				MB_ICONERROR | MB_OK);
			PostQuitMessage(-1);
		}
		return 0;
	}

	struct Win32ErrorChecker
	{};

	std::wstring
	getErrorDescription(DWORD dw);

	static const inline Win32ErrorChecker errorChecker;

	template <typename T>
	T
	operator>>(T result, Win32ErrorChecker)
	{
		if (GetLastError() == NO_ERROR)
		{
			return result;
		}

		if constexpr (std::is_integral_v<T>)
		{
			if (result == 0)
			{
				DWORD err = GetLastError();
				throw core::except::BerniniException(
					"Win32 API Error",
					core::str::wide_to_string(getErrorDescription(err)));
			}
		}
		else if constexpr (std::is_pointer_v<T>)
		{
			if (result == nullptr)
			{
				DWORD err = GetLastError();
				throw core::except::BerniniException(
					"Win32 API Error",
					core::str::wide_to_string(getErrorDescription(err)));
			}
		}
		else
		{
			static_assert(
				std::is_integral_v<T> || std::is_pointer_v<T>,
				"Win32ErrorChecker can only be used with integral or pointer return types.");
		}

		return result;
	}

	void
	operator>>(HRESULT hr, Win32ErrorChecker);

}
