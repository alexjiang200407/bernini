#include <core/log/ScopedStage.h>

#include <spdlog/spdlog.h>

namespace core::logging
{
	namespace
	{
		/**
		 * Writes at info through the default logger's sinks, whatever level that logger carries.
		 *
		 * The default logger's level is the *renderer's*: `init_file_logger` applies
		 * `GraphicsOptions::logLevel`, which is `kError` unless a config says otherwise. Logging
		 * through the logger would therefore drop every stage in the editor, and a cook diagnostic
		 * is not renderer chatter to be silenced by the same knob.
		 */
		void
		log_stage(const std::string& name, const double ms)
		{
			const std::shared_ptr<spdlog::logger> current = spdlog::default_logger();

			auto stage = spdlog::logger("stage", current->sinks().begin(), current->sinks().end());
			stage.set_level(spdlog::level::info);
			stage.info("{}: {:.1f} ms", name, ms);

			// Nothing flushes this logger on its behalf, and a bake that dies mid-run is exactly
			// when its stage lines are worth having.
			stage.flush();
		}
	}

	ScopedStage::ScopedStage(Formatted, std::string name) noexcept :
		m_Name(std::move(name)), m_Start(std::chrono::steady_clock::now())
	{}

	ScopedStage::~ScopedStage()
	{
		const std::chrono::duration<double, std::milli> elapsed = Elapsed();

		// A sink that throws must not take the stage's caller down with it: a bake that finished
		// has finished, and this line is a diagnostic.
		try
		{
			log_stage(m_Name, elapsed.count());
		}
		catch (...)
		{}
	}

	std::chrono::duration<double, std::milli>
	ScopedStage::Elapsed() const noexcept
	{
		return std::chrono::steady_clock::now() - m_Start;
	}
}
