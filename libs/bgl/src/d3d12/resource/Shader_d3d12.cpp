#include "resource/Shader_d3d12.h"
#include "device/Device_d3d12.h"

namespace bgl
{
	Shader::Shader(ShaderDesc desc, const Device* device) :
		m_Desc(std::move(desc)), m_Device(device)
	{
		gassert(
			m_Desc.slangModuleName.empty() == false,
			"Shader must have a valid Slang module name");

		gassert(device != nullptr, "Device cannot be null");
	}

	slang::IModule*
	Shader::GetSlangModule() const noexcept
	{
		SlangErrorChecker errChecker;

		slang::IModule* module = m_Device->GetSlangSession()->loadModule(
			m_Desc.slangModuleName.c_str(),
			errChecker.WriteDiagnosticBlob());
		errChecker.ReportError();

		return module;
	}
}
