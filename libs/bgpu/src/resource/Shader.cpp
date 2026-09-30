#include <bgpu/GpuContext.h>
#include <bgpu/resource/Shader.h>
#include <core/err/util.h>
#include <slang.h>
#include <utility>

namespace bgpu
{
	Shader::Shader(ShaderDesc desc, bgpu::GpuContextRef context) :
		m_Desc(std::move(desc)), m_Context(std::move(context))
	{
		core::ensure(
			m_Desc.slangModuleName.empty() == false,
			"Shader must have a valid Slang module name");
		core::ensure(m_Context != nullptr, "GPU context cannot be null");
	}

	slang::IModule*
	Shader::GetSlangModule() const noexcept
	{
		return m_Context->LoadModule(m_Desc.slangModuleName);
	}
}
