#include <bgpu/SlangErrorChecker.h>
#include <core/log/bassert.h>
#include <slang.h>

namespace bgpu
{
	void
	operator>>(SlangResult res, const SlangErrorChecker& checker)
	{
		if (SLANG_FAILED(res))
		{
			if (!checker.ReportError())
			{
				core::logging::bfatal("Slang operation failed with no diagnostics available.");
			}
		}
	}

	bool
	SlangErrorChecker::ReportError() const
	{
		if (m_DiagnosticBlob)
		{
			const char* errorMessage = (const char*)m_DiagnosticBlob->getBufferPointer();
			core::logging::bfatal("Slang operation failed with error: {}", errorMessage);
		}

		return false;
	}
}
