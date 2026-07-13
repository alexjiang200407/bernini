#pragma once

#include <QProgressDialog>

namespace editor::test
{
	/** How long a helper here waits for the UI it is expecting before it gives up and returns. */
	inline constexpr int c_ModalTimeoutMs = 10'000;

	/** The loading screen currently on top, or null if none is up. */
	[[nodiscard]] QProgressDialog*
	ActiveLoadingScreen();

	/**
	 * Drives a loading screen from the UI thread while it is up.
	 *
	 * RunWithLoadingScreen does not return until its worker has finished, so a test cannot reach the
	 * screen after calling it, and must not touch widgets from the worker thread. This arms a poll
	 * that runs inside the nested event loop instead: once a screen appears, `action` is called with
	 * it, repeatedly, until it returns true.
	 *
	 * Arm it *before* the RunWithLoadingScreen call it is meant to drive. The poll disarms itself
	 * after c_ModalTimeoutMs whether or not `action` ever succeeded, so a test that goes wrong fails
	 * on its own assertions rather than hanging the suite.
	 */
	void
	OnLoadingScreen(std::function<bool(QProgressDialog&)> action);

	/** Presses Cancel on the loading screen as soon as one is up. */
	void
	CancelLoadingScreen();
}
