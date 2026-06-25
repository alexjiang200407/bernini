#include "slang/ErrorChecker.h"
#include <slang.h>

namespace bgl
{
	void
	operator>>(SlangResult res, const SlangErrorChecker& checker)
	{
		if (SLANG_FAILED(res))
		{
			if (!checker.ReportError())
			{
				gfatal("Slang operation failed with no diagnostics available.");
			}
		}
	}

	bool
	SlangErrorChecker::ReportError() const
	{
		if (m_DiagnosticBlob)
		{
			const char* errorMessage = (const char*)m_DiagnosticBlob->getBufferPointer();
			gfatal("Slang operation failed with error: {}", errorMessage);
		}

		return false;
	}
}
