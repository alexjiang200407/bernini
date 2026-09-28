#include <bgl_common/gassert.h>
#include <bgpu/SlangErrorChecker.h>
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
				bgl::gfatal("Slang operation failed with no diagnostics available.");
			}
		}
	}

	bool
	SlangErrorChecker::ReportError() const
	{
		if (m_DiagnosticBlob)
		{
			const char* errorMessage = (const char*)m_DiagnosticBlob->getBufferPointer();
			bgl::gfatal("Slang operation failed with error: {}", errorMessage);
		}

		return false;
	}
}
