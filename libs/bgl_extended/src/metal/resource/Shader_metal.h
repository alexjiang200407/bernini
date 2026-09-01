#pragma once
#include "resource/Shader.h"

namespace bgl
{
	// The Metal shader handle. Like the D3D12 twin it is backend-agnostic in substance -- a lazy
	// front-end compile of one Slang module; codegen to MSL happens per-PSO in the pipeline.
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
		GetSlangModule() const noexcept override;

		const ShaderDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

	private:
		ShaderDesc                            m_Desc;
		slang::ISession*                      m_Session = nullptr;
		mutable Slang::ComPtr<slang::IModule> m_SlangModule;
	};
}
