#pragma once

// Windows only, and empty elsewhere: the one D3D12 user in the tree includes it unconditionally
// through its PCH, and a public header has to parse on every host the tools run on.
#if defined(_WIN32)

#	include <device_context/api.h>
#	include <string>

#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>

namespace gpu
{
	/** `hr >> c_D3d12ErrChecker` logs a failed HRESULT's system description and aborts. */
	struct D3d12ErrorChecker
	{};

	inline constexpr D3d12ErrorChecker c_D3d12ErrChecker;

	DEVICE_CONTEXT_API std::wstring
					   GetErrorDescription(HRESULT hr);

	DEVICE_CONTEXT_API void
	operator>>(HRESULT hr, D3d12ErrorChecker);
}

#endif
