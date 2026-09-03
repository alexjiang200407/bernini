#include "slang/SlangSessions.h"
#include <bgl_common/SlangErrorChecker.h>

namespace bgl
{
	SlangSessions::SlangSessions(SlangSessionDesc desc) noexcept : m_Desc(desc) {}

	slang::ISession*
	SlangSessions::ForThisThread() noexcept
	{
		{
			const auto held  = std::lock_guard(m_Mutex);
			const auto found = m_ByThread.find(std::this_thread::get_id());
			if (found != m_ByThread.end())
				return found->second.session.get();
		}

		// Created outside the lock: distinct global sessions are independent, and creating one is
		// the load of the core module -- the one step here worth running several of at once.
		ThreadSessions mine;
		slang::createGlobalSession(mine.global.writeRef());
		gassert(mine.global != nullptr, "Failed to create Slang global session");

		slang::SessionDesc sessionDesc = {};
		slang::TargetDesc  targetDesc  = {};

		targetDesc.format  = m_Desc.target;
		targetDesc.profile = mine.global->findProfile("sm_6_6");

		sessionDesc.targetCount     = 1;
		sessionDesc.targets         = &targetDesc;
		sessionDesc.searchPaths     = m_Desc.searchPaths.data();
		sessionDesc.searchPathCount = static_cast<SlangInt>(m_Desc.searchPaths.size());

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

		const auto held = std::lock_guard(m_Mutex);
		return m_ByThread.insert_or_assign(std::this_thread::get_id(), std::move(mine))
		    .first->second.session.get();
	}

	void
	SlangSessions::ReleaseAll() noexcept
	{
		const auto held = std::lock_guard(m_Mutex);
		m_ByThread.clear();
	}
}
