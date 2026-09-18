#include "resource/Shader.h"
#include "slang/SlangSessions.h"
#include <bgl_common/gassert.h>
#include <slang.h>
#include <utility>

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
		return m_Sessions->LoadModule(m_Desc.slangModuleName);
	}
}
