#pragma once

namespace bgl
{
	class SlangErrorChecker
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

	void
	operator>>(SlangResult res, const SlangErrorChecker& checker);
}
