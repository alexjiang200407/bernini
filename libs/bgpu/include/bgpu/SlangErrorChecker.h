#pragma once
#include <bgpu/api.h>
#include <slang-com-ptr.h>
#include <slang.h>

namespace bgpu
{
	/** `result >> checker` ends the process on a failed Slang call, with its diagnostics. */
	class BGPU_API SlangErrorChecker
	{
	public:
		SlangErrorChecker() = default;

		slang::IBlob**
		WriteDiagnosticBlob()
		{
			return m_DiagnosticBlob.writeRef();
		}

		slang::IBlob*
		GetDiagnosticBlob() const
		{
			return m_DiagnosticBlob.get();
		}

		bool
		ReportError() const;

	private:
		Slang::ComPtr<slang::IBlob> m_DiagnosticBlob;
	};

	BGPU_API void
	operator>>(SlangResult res, const SlangErrorChecker& checker);
}
