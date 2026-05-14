#include "resource/Shader_d3d12.h"
#include "resource/Shader.h"

namespace bgl
{
	const std::byte*
	Shader::GetBytecode() const
	{
		gassert(IsInitialized(), "Shader must be initialized to get bytecode.");
		return GetImpl()->GetBytecode();
	}

	size_t
	Shader::GetBytecodeSize() const
	{
		gassert(IsInitialized(), "Shader must be initialized to get bytecode size.");
		return GetImpl()->GetBytecodeSize();
	}

}
