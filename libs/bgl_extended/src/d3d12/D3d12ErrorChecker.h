#pragma once
#include <core/str/str.h>

namespace bgl
{
	struct D3d12ErrorChecker
	{};

	inline constexpr D3d12ErrorChecker d3d12ErrChecker;

	std::wstring
	GetErrorDescription(HRESULT hr);

	void
	operator>>(HRESULT hr, D3d12ErrorChecker);
}
