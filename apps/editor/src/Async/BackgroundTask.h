#pragma once

#include <QString>

#include <functional>

class QWidget;

namespace background
{
	class ProgressRelay;

	/**
	 * Handed to a background worker so it can drive the loading screen. Report() is safe to call
	 * from the worker thread: it marshals onto the UI thread and returns immediately.
	 */
	class Progress
	{
	public:
		// `total == 0` leaves the bar in its indeterminate busy state, for a step whose length is
		// unknown (parsing a glTF, say). An empty `label` keeps the current one.
		void
		Report(int done, int total, const QString& label = QString());

	private:
		friend bool
		RunWithLoadingScreen(
			QWidget*,
			const QString&,
			const std::function<void(Progress&)>&,
			QString*);

		explicit Progress(ProgressRelay* relay) : m_Relay(relay) {}

		ProgressRelay* m_Relay = nullptr;
	};

	/**
	 * Runs `work` on a worker thread behind a modal loading screen, and returns once it finishes.
	 *
	 * `work` must touch only thread-safe state. Nothing in bgl qualifies -- Scene and
	 * ResourceManager carry no locks and the frame graph is documented single-threaded -- so a
	 * worker may decode and parse, but every scene mutation has to happen after this returns.
	 *
	 * @return true when `work` ran to completion; false when it threw, in which case `error`
	 *         (when given) receives the exception's message.
	 */
	bool
	RunWithLoadingScreen(
		QWidget*                              parent,
		const QString&                        title,
		const std::function<void(Progress&)>& work,
		QString*                              error = nullptr);
}
