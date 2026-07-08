#pragma once
#include "resource/Shader.h"

namespace bgl
{
	class Shader final : public core::RefCounter<IShader>
	{
	public:
		Shader(ShaderDesc desc, slang::ISession* session);
		~Shader() noexcept override { logger::trace("~Shader"); }
		Shader(const Shader&)     = delete;
		Shader(Shader&&) noexcept = delete;

		Shader&
		operator=(const Shader&) = delete;

		Shader&
		operator=(Shader&&) noexcept = delete;

		slang::IModule*
		GetSlangModule() const noexcept override
		{
			return m_SlangModule;
		}

		const ShaderDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

	private:
		ShaderDesc                    m_Desc;
		Slang::ComPtr<slang::IModule> m_SlangModule = nullptr;
	};
}
