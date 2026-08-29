#pragma once
#include <bgl/IGraphics.h>

namespace bgl
{
	/**
	 * The startup pipeline build, counted. A pass steps this once per pipeline it is about to
	 * create, so the name reaches the client's screen before the compile that takes the time.
	 *
	 * The total is declared before the first step, because a bar that cannot be sized is a bar
	 * that cannot say how much is left; `Complete()` checks it against the steps that followed, so
	 * a pass whose `c_Pipelines` no longer matches what it builds is caught where it is introduced
	 * rather than seen as a bar that fills early.
	 *
	 * Not thread-safe: pipelines are built one at a time on the thread constructing the Graphics.
	 */
	class PipelineBuild
	{
	public:
		PipelineBuild(PipelineProgressFn onProgress, uint32_t total) noexcept :
			m_OnProgress(std::move(onProgress)), m_Total(total)
		{}

		/** Reports `name` as the pipeline about to be built, and counts it. */
		void
		Step(std::string_view name)
		{
			if (m_OnProgress)
				m_OnProgress(PipelineProgress{ m_Done, m_Total, name });
			++m_Done;
		}

		[[nodiscard]] bool
		Complete() const noexcept
		{
			return m_Done == m_Total;
		}

		[[nodiscard]] uint32_t
		GetStepped() const noexcept
		{
			return m_Done;
		}

	private:
		PipelineProgressFn m_OnProgress;
		uint32_t           m_Total = 0;
		uint32_t           m_Done  = 0;
	};
}
