#pragma once

namespace core::logging
{
	/**
	 * Times a named stage and logs how long it took, when the scope ends.
	 *
	 * The line carries its own duration rather than only a timestamp, so attributing a slow run
	 * never means subtracting one log entry from another -- which here would mean subtracting
	 * across two files with two clocks, since the editor writes `editor.log` through Qt and
	 * `bgl.log` through spdlog. Timestamps still answer what happened *between* stages.
	 *
	 * Writes at info through the default logger's *sinks*, and deliberately not through the logger,
	 * whose level belongs to the renderer -- `init_file_logger` sets it from
	 * `GraphicsOptions::logLevel`, which is `kError` unless a config says otherwise. So a stage
	 * lands in whichever file the process logs to without being silenced by a knob that is not
	 * about cooking.
	 */
	class ScopedStage
	{
	public:
		/** @param name Written verbatim; format the dimensions that explain the cost into it. */
		explicit ScopedStage(std::string name) noexcept;

		/**
		 * Stays silent when the stage finishes faster than `quietBelow`, which is what makes the
		 * timer usable on a path that runs every frame.
		 */
		ScopedStage(std::string name, std::chrono::milliseconds quietBelow) noexcept;

		~ScopedStage();

		ScopedStage(const ScopedStage&) = delete;
		ScopedStage(ScopedStage&&)      = delete;
		ScopedStage&
		operator=(const ScopedStage&) = delete;
		ScopedStage&
		operator=(ScopedStage&&) = delete;

		/** How long the stage has run so far -- for a caller that must split its own total. */
		[[nodiscard]] std::chrono::duration<double, std::milli>
		Elapsed() const noexcept;

	private:
		std::string                           m_Name;
		std::chrono::steady_clock::time_point m_Start;
		std::chrono::milliseconds             m_QuietBelow;
	};
}
