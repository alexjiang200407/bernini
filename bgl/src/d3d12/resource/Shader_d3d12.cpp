#include "resource/Shader_d3d12.h"

namespace bgl
{
	Shader::Shader(ShaderDesc desc, slang::ISession* session) : m_Desc(std::move(desc))
	{
		gassert(
			m_Desc.slangModuleName.empty() == false,
			"Shader must have a valid Slang module name");

		gassert(session != nullptr, "Slang session cannot be null");

		SlangErrorChecker errChecker;
		m_SlangModule.attach(
			session->loadModule(m_Desc.slangModuleName.c_str(), errChecker.WriteDiagnosticBlob()));

		errChecker.ReportError();
	}
}
