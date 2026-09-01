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

	/**
	 * Holds the NSError a Metal call writes, so a call site reads like its D3D12 and Slang
	 * counterparts. Metal reports failure by returning nil, so the returned object is the status:
	 *
	 *     MetalErrorChecker errChecker;
	 *     auto library = NS::TransferPtr(device->newLibrary(src, nullptr, errChecker.WriteError()));
	 *     library.get() >> errChecker;
	 *
	 * For the calls that take no error out-param -- most of them -- there is nothing to hold and a
	 * `gassert` on the returned pointer is the whole check.
	 */
	class MetalErrorChecker
	{
	public:
		MetalErrorChecker() = default;

		NS::Error**
		WriteError() noexcept
		{
			return &m_Error;
		}

		[[nodiscard]] const NS::Error*
		GetError() const noexcept
		{
			return m_Error;
		}

		bool
		ReportError() const;

	private:
		NS::Error* m_Error = nullptr;
	};

	void
	operator>>(const void* object, const MetalErrorChecker& checker);
}
