#include "resource/Shader.h"
#include "slang/SlangErrorChecker.h"
#include "slang/SlangSessions.h"

namespace bgl
{
	Shader::Shader(ShaderDesc desc, SlangSessions* sessions) :
		m_Desc(std::move(desc)), m_Sessions(sessions)
	{
		gassert(
			m_Desc.slangModuleName.empty() == false,
			"Shader must have a valid Slang module name");
		gassert(sessions != nullptr, "Slang sessions cannot be null");
	}

	slang::IModule*
	Shader::GetSlangModule() const noexcept
	{
		SlangErrorChecker errChecker;

		slang::IModule* module = m_Sessions->ForThisThread()->loadModule(
			m_Desc.SlangModulePath().c_str(),
			errChecker.WriteDiagnosticBlob());
		errChecker.ReportError();

		return module;
	}
}
