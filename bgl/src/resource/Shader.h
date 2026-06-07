#pragma once

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	struct ShaderDesc
	{
		std::vector<std::byte> bytecode;
		std::string            debugName;
	};

	class IShader : public core::Ref
	{
	public:
		virtual const std::byte*
		GetBytecode() const = 0;

		virtual size_t
		GetBytecodeSize() const = 0;
	};

	using ShaderHandle = core::SharedRef<IShader>;
}
