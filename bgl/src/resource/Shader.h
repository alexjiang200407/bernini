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
		SetBytecode(std::vector<std::byte> _bytecode)
		{
			this->bytecode = std::move(_bytecode);
			return *this;
		}

		ShaderDesc&
		SetSlangModuleName(std::string _slangModuleName)
		{
			this->slangModuleName = std::move(_slangModuleName);
			return *this;
		}

		ShaderDesc&
		SetDebugName(std::string _debugName)
		{
			this->debugName = std::move(_debugName);
			return *this;
		}
	};

	class IShader : public core::Ref
	{
	public:
		IShader() noexcept = default;

		IShader(const IShader&) = delete;
		IShader(IShader&&)      = delete;

		IShader&
		operator=(const IShader&) = delete;

		IShader&
		operator=(IShader&&) = delete;

		virtual const std::byte*
		GetBytecode() const = 0;

		virtual size_t
		GetBytecodeSize() const = 0;

		virtual slang::IModule*
		GetSlangModule() const = 0;
	};

	using ShaderHandle = core::SharedRef<IShader>;
}
