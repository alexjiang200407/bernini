#pragma once
#include "resource/Shader.h"

namespace bgl
{
	class Shader final : public core::RefCounter<IShader>
	{
	public:
		Shader(ShaderDesc desc, slang::ISession* session);

		Shader(const Shader&)     = delete;
		Shader(Shader&&) noexcept = delete;

		Shader&
		operator=(const Shader&) = delete;

		Shader&
		operator=(Shader&&) noexcept = delete;

		const std::byte*
		GetBytecode() const override
		{
			return m_Desc.bytecode.data();
		}

		size_t
		GetBytecodeSize() const override
		{
			return m_Desc.bytecode.size();
		}

		slang::IModule*
		GetSlangModule() const override
		{
			return m_SlangModule.get();
		}

	private:
		ShaderDesc                    m_Desc;
		Slang::ComPtr<slang::IModule> m_SlangModule = nullptr;
	};
}
