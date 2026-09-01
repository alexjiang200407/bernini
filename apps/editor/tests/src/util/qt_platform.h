#pragma once

namespace editor::test
{
	/**
	 * Whether a QPA platform was named for this run, by `-platform` or by `QT_QPA_PLATFORM`, rather
	 * than left to the default `main` applies.
	 *
	 * Answered from the state on entry to `main`, before that default is written.
	 */
	[[nodiscard]] bool
	PlatformWasNamed() noexcept;
}
