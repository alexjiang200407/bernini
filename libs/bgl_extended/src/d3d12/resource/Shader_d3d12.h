#pragma once
#include "resource/Shader.h"

namespace bgl
{
	class Device;

	class Shader final : public core::RefCounter<IShader>
	{
	public:
		Shader(ShaderDesc desc, const Device* device);
		~Shader() noexcept override { logger::trace("~Shader"); }
		Shader(const Shader&)     = delete;
		Shader(Shader&&) noexcept = delete;

		Shader&
		operator=(const Shader&) = delete;

		Shader&
		operator=(Shader&&) noexcept = delete;

		// Front-end-compiles the slang module on first use. Deferring it lets a shader
		// cache hit build a pipeline without ever parsing the source (the dominant
		// compile cost), since the module is only touched when a PSO must recompile.
		//
		// The result is deliberately not cached here: the session already caches its modules by
		// name, and a ref held past Device::ReleaseSlangSession would pin the whole session.
		slang::IModule*
		GetSlangModule() const noexcept override;

		const ShaderDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

	private:
		ShaderDesc    m_Desc;
		const Device* m_Device = nullptr;
	};
}
