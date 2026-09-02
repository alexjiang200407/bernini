#pragma once
#include <RmlUi/Core/SystemInterface.h>

namespace game
{
	/**
	 * RmlUi's clock, log and path resolution.
	 *
	 * The clock is the client's, advanced by `UiRuntime::AdvanceTime` and never read from the wall
	 * (ADR-9), so a headless test steps a transition exactly rather than racing it.
	 */
	class UiSystemInterface final : public Rml::SystemInterface
	{
	public:
		double
		GetElapsedTime() override;

		bool
		LogMessage(Rml::Log::Type type, const Rml::String& message) override;

		/**
		 * Resolves a document's `@import`, `src` or `font-face` reference against the document's own
		 * key, into the mount-key form every other reference in the project is stored in: no `..`,
		 * no leading `/`, `/`-separated (ADR-8).
		 *
		 * A source carrying a scheme -- `target://preview` -- passes through untouched, because it
		 * names something the game registered rather than a file.
		 *
		 * A reference that leaves the data root yields an empty path rather than an exception:
		 * RmlUi calls this from inside its own parse, and the open that follows reports the failure
		 * with the document and line already in hand.
		 */
		void
		JoinPath(
			Rml::String&       translatedPath,
			const Rml::String& documentPath,
			const Rml::String& path) override;

		void
		Advance(double seconds) noexcept
		{
			m_Elapsed += seconds;
		}

		[[nodiscard]] double
		Elapsed() const noexcept
		{
			return m_Elapsed;
		}

	private:
		double m_Elapsed = 0.0;
	};

	// Whether `source` carries a URI scheme -- `target://preview`. JoinPath leaves one alone rather
	// than resolving it as a key; which schemes actually mean something is the resolver's to say,
	// and one it does not know fails there instead of being mangled into a path first.
	[[nodiscard]] bool
	IsSchemeSource(std::string_view source) noexcept;
}
