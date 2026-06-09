#pragma once

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	struct ShaderDesc
	{
		std::vector<std::byte> bytecode;
		std::string            slangModuleName;
		std::string            debugName;

		ShaderDesc&
		SetBytecode(std::vector<std::byte> bytecode)
		{
			this->bytecode = std::move(bytecode);
			return *this;
		}

		ShaderDesc&
		SetSlangModuleName(std::string slangModuleName)
		{
			this->slangModuleName = std::move(slangModuleName);
			return *this;
		}

		ShaderDesc&
		SetDebugName(std::string debugName)
		{
			this->debugName = std::move(debugName);
			return *this;
		}
	};

	class IShader : public core::Ref
	{
	public:
		virtual const std::byte*
		GetBytecode() const = 0;

		virtual size_t
		GetBytecodeSize() const = 0;

		virtual slang::IModule*
		GetSlangModule() const = 0;
	};

	using ShaderHandle = core::SharedRef<IShader>;
}
