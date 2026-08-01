#pragma once
#include "metal_cpp.h"

namespace bgl
{
	/**
	 * The domain, code and description carried by `error`, or a fixed string when it carries none.
	 *
	 * Metal signals a failure by returning nil and *optionally* filling the error, so a call can fail
	 * with no diagnosis at all -- reading through the error is then the crash that hides the failure
	 * it was meant to report.
	 */
	[[nodiscard]] std::string
	GetErrorDescription(const NS::Error* error);

	[[noreturn]] void
	ReportMetalFailure(std::string_view what, const NS::Error* error);

	/**
	 * Returns `object`, or terminates naming `what` and whatever the error says.
	 *
	 * The counterpart of `d3d12ErrChecker`, keyed on the returned pointer rather than on a status:
	 * D3D12 hands back an HRESULT alongside the object, while a Metal creation call reports failure
	 * by returning nil and most take no error out-param at all. So the null check *is* the whole
	 * contract, and an unchecked one surfaces as a null dereference somewhere downstream instead.
	 *
	 * For the failures a caller can act on -- a shader that will not compile -- throw with
	 * GetErrorDescription instead; this is for the ones that mean the device is unusable.
	 */
	template <typename T>
	[[nodiscard]] T*
	MetalCheck(T* object, const std::string_view what, const NS::Error* error = nullptr)
	{
		if (object == nullptr)
			ReportMetalFailure(what, error);

		return object;
	}
}
