#pragma once
#include <core/str/str.h>

namespace bgl
{
	struct ErrorChecker
	{};

	static const inline ErrorChecker d3d12ErrChecker;

	std::wstring
	GetErrorDescription(HRESULT hr);

	void
	operator>>(HRESULT hr, ErrorChecker);
}
