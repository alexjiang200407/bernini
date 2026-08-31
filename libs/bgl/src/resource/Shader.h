#pragma once

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	struct ShaderDesc
	{
		std::string slangModuleName;
		std::string entryPointName;
		std::string debugName;

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

		/**
		 * The module name as `ISession::loadModule` wants it: `/`-separated, not `.`-separated.
		 * `loadModule` appends `.slang` to the string it is given and opens that, where `import`
		 * translates the dots itself -- so the one spelling every caller and every shader uses has
		 * to be converted here, at the only point that reaches Slang's file loader.
		 */
		[[nodiscard]] std::string
		SlangModulePath() const noexcept
		{
			std::string path = slangModuleName;
			std::ranges::replace(path, '.', '/');
			return path;
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

		virtual slang::IModule*
		GetSlangModule() const noexcept = 0;

		virtual const ShaderDesc&
		GetDesc() const noexcept = 0;
	};

	using ShaderRef = core::SharedRef<IShader>;
}
