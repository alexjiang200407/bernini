#pragma once

#include <algorithm>
#include <core/ref/Ref.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <slang.h>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

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

	class SlangSessions;

	/**
	 * The one IShader: a module name and an entry point, resolved through whichever Slang session
	 * belongs to the thread that compiles.
	 */
	class Shader final : public core::RefCounter<IShader>
	{
	public:
		/** @pre `sessions` outlives the shader; the device that owns both guarantees it. */
		Shader(ShaderDesc desc, SlangSessions* sessions);
		~Shader() noexcept override { logger::trace("~Shader"); }
		Shader(const Shader&)     = delete;
		Shader(Shader&&) noexcept = delete;

		Shader&
		operator=(const Shader&) = delete;

		Shader&
		operator=(Shader&&) noexcept = delete;

		// Front-end-compiles the module on every call, on the calling thread's session. Deferring
		// it lets a shader-cache hit build a pipeline without ever parsing the source (the dominant
		// compile cost), since the module is only touched when a PSO must recompile.
		//
		// Deliberately not memoized: the session already caches its modules by name, a module
		// belongs to one thread's session, and a ref held past ReleaseSlangSession would pin the
		// whole session.
		slang::IModule*
		GetSlangModule() const noexcept override;

		const ShaderDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

	private:
		ShaderDesc     m_Desc;
		SlangSessions* m_Sessions = nullptr;
	};
}
