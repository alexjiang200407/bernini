#include "resource/Shader.h"
#include <bgl_common/gassert.h>
#include <device_context/DeviceContext.h>
#include <slang.h>
#include <utility>

namespace bgl
{
	Shader::Shader(ShaderDesc desc, gpu::DeviceContext* context) :
		m_Desc(std::move(desc)), m_Context(context)
	{
		gassert(
			m_Desc.slangModuleName.empty() == false,
			"Shader must have a valid Slang module name");
		gassert(context != nullptr, "Device context cannot be null");
	}

	slang::IModule*
	Shader::GetSlangModule() const noexcept
	{
		return m_Context->LoadModule(m_Desc.slangModuleName);
	}
}
