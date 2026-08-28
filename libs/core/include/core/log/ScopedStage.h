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
	 *
	 * The name is a format string, because a stage is worth timing only when the line also says
	 * what the cost was a product of:
	 *
	 *     const auto stage = ScopedStage("posed bounds: {} bones, {} frames", bones, frames);
	 *     const auto tick  = ScopedStage(50ms, "thumbnail tick: {}", path);
	 */
	class ScopedStage
	{
	public:
		template <typename... Args>
		explicit ScopedStage(std::format_string<Args...> name, Args&&... args) :
			ScopedStage(
				Formatted(),
				std::chrono::milliseconds(0),
				std::format(name, std::forward<Args>(args)...))
		{}

		/**
		 * Stays silent when the stage finishes faster than `quietBelow`, which is what makes the
		 * timer usable on a path that runs every frame. Leading, so it cannot be mistaken for a
		 * format argument.
		 */
		template <typename... Args>
		ScopedStage(
			const std::chrono::milliseconds quietBelow,
			std::format_string<Args...>     name,
			Args&&... args) :
			ScopedStage(Formatted(), quietBelow, std::format(name, std::forward<Args>(args)...))
		{}

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
		// A literal binds to `std::string` better than it deduces a format string, so the delegating
		// target has to be unreachable by overload resolution rather than merely private.
		struct Formatted
		{};

		ScopedStage(Formatted, std::chrono::milliseconds quietBelow, std::string name) noexcept;

		std::string                           m_Name;
		std::chrono::steady_clock::time_point m_Start;
		std::chrono::milliseconds             m_QuietBelow;
	};
}
