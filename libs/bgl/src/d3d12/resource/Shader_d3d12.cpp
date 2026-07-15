#include "resource/Shader_d3d12.h"

namespace bgl
{
	Shader::Shader(ShaderDesc desc, slang::ISession* session) :
		m_Desc(std::move(desc)), m_Session(session)
	{
		gassert(
			m_Desc.slangModuleName.empty() == false,
			"Shader must have a valid Slang module name");

		gassert(session != nullptr, "Slang session cannot be null");
	}

	slang::IModule*
	Shader::GetSlangModule() const noexcept
	{
		if (m_SlangModule == nullptr)
		{
			SlangErrorChecker errChecker;
			m_SlangModule = m_Session->loadModule(
				m_Desc.slangModuleName.c_str(),
				errChecker.WriteDiagnosticBlob());
			errChecker.ReportError();
		}

		return m_SlangModule;
	}
}
