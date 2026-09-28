#include <bgpu/SlangErrorChecker.h>
#include <core/err/util.h>
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
				core::fatal("Slang operation failed with no diagnostics available.");
			}
		}
	}

	bool
	SlangErrorChecker::ReportError() const
	{
		if (m_DiagnosticBlob)
		{
			const char* errorMessage = (const char*)m_DiagnosticBlob->getBufferPointer();
			core::fatal("Slang operation failed with error: {}", errorMessage);
		}

		return false;
	}
}
