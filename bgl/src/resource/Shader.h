#pragma once
#include <core/pimpl/RefCountPImpl.h>

namespace bgl
{
	struct ShaderDesc
	{
		std::vector<std::byte> bytecode;
		std::string            debugName;
	};

	class ShaderImpl;
	class Shader : private core::RefCountPImpl<ShaderImpl>
	{
	public:
		Shader() = default;

		const std::byte*
		GetBytecode() const;

		size_t
		GetBytecodeSize() const;

		using core::RefCountPImpl<ShaderImpl>::IsInitialized;

	private:
		friend class DeviceImpl;
	};
}
