#pragma once
#include "resource/Shader.h"

namespace bgl
{
	class ShaderImpl final
	{
	public:
		ShaderImpl(const ShaderDesc& desc) : m_Desc(desc) {}
		ShaderImpl(ShaderDesc&& desc) : m_Desc(std::move(desc)) {}

		const std::byte*
		GetBytecode() const
		{
			return m_Desc.bytecode.data();
		}

		size_t
		GetBytecodeSize() const
		{
			return m_Desc.bytecode.size();
		}

	private:
		ShaderDesc m_Desc;

		friend class Shader;
	};
}
