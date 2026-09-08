#include "slang/SlangSessions.h"
#include <algorithm>
#include <bgl_common/SlangErrorChecker.h>
#include <bgl_common/gassert.h>
#include <core/err/util.h>
#include <filesystem>
#include <mutex>
#include <optional>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace bgl
{
	std::string
	SlangModulePath(std::string_view moduleName)
	{
		std::string path(moduleName);
		std::ranges::replace(path, '.', '/');
		return path;
	}

	namespace
	{
		// Every other loader here reports a diagnostic through SlangErrorChecker, which ends in
		// gfatal -- right for the engine's own shaders, where a diagnostic is a bug. These two load
		// text the client wrote, where it is a message to hand back.
		slang::IModule*
		LoadReporting(
			slang::ISession* session,
			std::string_view moduleName,
			std::string&     diagnostic)
		{
			Slang::ComPtr<slang::IBlob> blob;
			slang::IModule*             module =
				session->loadModule(SlangModulePath(moduleName).c_str(), blob.writeRef());

			if (blob != nullptr)
				diagnostic = static_cast<const char*>(blob->getBufferPointer());

			return module;
		}
	}

	std::vector<std::string>
	ShaderSearchPaths(const std::filesystem::path& clientDir)
	{
		std::vector<std::string> paths = { "./shaders/src", "./shaders/tests" };
		if (!clientDir.empty())
			paths.push_back(clientDir.string());
		return paths;
	}

	SlangSessions::SlangSessions(SlangSessionDesc desc) noexcept : m_Desc(std::move(desc)) {}

	slang::ISession*
	SlangSessions::ForThisThread() noexcept
	{
		SlangSessionDesc desc;
		{
			const auto held  = std::lock_guard(m_Mutex);
			const auto found = m_ByThread.find(std::this_thread::get_id());
			if (found != m_ByThread.end())
				return found->second.session.get();
			desc = m_Desc;
		}

		// Created outside the lock: distinct global sessions are independent, and creating one is
		// the load of the core module -- the one step here worth running several of at once.
		ThreadSessions mine;
		slang::createGlobalSession(mine.global.writeRef());
		gassert(mine.global != nullptr, "Failed to create Slang global session");

		slang::SessionDesc sessionDesc = {};
		slang::TargetDesc  targetDesc  = {};

		targetDesc.format  = desc.target;
		targetDesc.profile = mine.global->findProfile("sm_6_6");

		std::vector<const char*> searchPaths;
		searchPaths.reserve(desc.searchPaths.size());
		for (const std::string& path : desc.searchPaths) searchPaths.push_back(path.c_str());

		sessionDesc.targetCount     = 1;
		sessionDesc.targets         = &targetDesc;
		sessionDesc.searchPaths     = searchPaths.data();
		sessionDesc.searchPathCount = static_cast<SlangInt>(searchPaths.size());

		// Match the column-major convention the CPU side uploads matrices in (and that the offline
		// slangc default used). The API's SessionDesc otherwise defaults to row-major, which would
		// transpose viewProj / transforms and project geometry off screen.
		sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

#if defined(BERNINI_GPU_DEBUG)
		// Enables dbg_raise() bodies and the cull-stats counters in runtime-compiled shaders. Kept
		// in lockstep with the offline slangc -D in cmake/compile_shader.cmake. Fully absent in
		// Release, so gDebug drops out of reflection and dbg_raise becomes a no-op.
		const slang::PreprocessorMacroDesc debugMacro = { "BERNINI_GPU_DEBUG", "1" };
		sessionDesc.preprocessorMacros                = &debugMacro;
		sessionDesc.preprocessorMacroCount            = 1;
#endif

		SlangErrorChecker errChecker;
		mine.global->createSession(sessionDesc, mine.session.writeRef()) >> errChecker;
		gassert(mine.session != nullptr, "Failed to create Slang session");

		// Loaded under the path form, which is what an import of a dotted name looks up: registered
		// as `game.probe` the text is never found and the file wins. The second argument is a name
		// for diagnostics, never opened.
		for (const SlangSourceModule& sourceModule : desc.sourceModules)
		{
			const std::string path = SlangModulePath(sourceModule.name);

			SlangErrorChecker moduleChecker;
			slang::IModule*   loaded = mine.session->loadModuleFromSourceString(
				path.c_str(),
				(path + ".slang").c_str(),
				sourceModule.source.c_str(),
				moduleChecker.WriteDiagnosticBlob());
			moduleChecker.ReportError();
			gassert(
				loaded != nullptr,
				"Failed to load Slang module '{}' from source",
				sourceModule.name);
		}

		const auto held = std::lock_guard(m_Mutex);
		return m_ByThread.insert_or_assign(std::this_thread::get_id(), std::move(mine))
		    .first->second.session.get();
	}

	std::optional<ReflectedSurface>
	SlangSessions::ReflectSurface(std::string_view moduleName, std::string_view surfaceName)
	{
		std::string     diagnostic;
		slang::IModule* module = LoadReporting(ForThisThread(), moduleName, diagnostic);
		if (module == nullptr)
		{
			core::throw_runtime_error(
				"surface '{}': its module did not compile\n{}",
				surfaceName,
				diagnostic);
		}

		return bgl::ReflectSurface(module, surfaceName);
	}

	void
	SlangSessions::ReleaseAll() noexcept
	{
		const auto held = std::lock_guard(m_Mutex);
		m_ByThread.clear();
	}

	void
	SlangSessions::AddSourceModule(SlangSourceModule sourceModule) noexcept
	{
		const auto held = std::lock_guard(m_Mutex);
		m_Desc.sourceModules.push_back(std::move(sourceModule));
		m_ByThread.clear();
	}
}
