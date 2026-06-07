#pragma once
#include "resource/Shader.h"

namespace bgl
{
	class Shader final : public core::RefCounter<IShader>
	{
	public:
		Shader(const ShaderDesc& desc) : m_Desc(desc) {}
		Shader(ShaderDesc&& desc) : m_Desc(std::move(desc)) {}

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

	private:
		ShaderDesc m_Desc;
	};
}
