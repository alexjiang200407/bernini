#pragma once

#include <QString>

#include <assetlib/cancel.h>

class QWidget;

namespace background
{
	class ProgressRelay;

	enum class Cancellable
	{
		kNo,   // no cancel button, and Esc / the title-bar X will not dismiss the screen either
		kYes,  // `work` must poll Progress::Cancellation(), or the button does nothing
	};

	enum class TaskStatus
	{
		kCompleted,
		kCancelled,
		kFailed,
	};

	struct TaskResult
	{
		TaskStatus status = TaskStatus::kFailed;
		QString    error;  // non-empty only when status == kFailed

		[[nodiscard]] bool
		Completed() const noexcept
		{
			return status == TaskStatus::kCompleted;
		}

		[[nodiscard]] bool
		Cancelled() const noexcept
		{
			return status == TaskStatus::kCancelled;
		}

		[[nodiscard]] bool
		Failed() const noexcept
		{
			return status == TaskStatus::kFailed;
		}
	};

	/**
	 * Handed to a background worker so it can drive the loading screen. Both methods are safe to call
	 * from the worker thread: Report() marshals onto the UI thread and returns immediately.
	 */
	class Progress
	{
	public:
		void
		Report(int done, int total, const QString& label = QString());

		/**
		 * Signalled when the user presses Cancel. Pass it to the assetlib cook the worker is running --
		 * they throw assetlib::Cancelled once it is set, which RunWithLoadingScreen reports as
		 * TaskStatus::kCancelled rather than as a failure.
		 *
		 * A worker that ignores it simply runs to completion; the button then does nothing.
		 */
		[[nodiscard]] assetlib::CancelToken
		Cancellation() const;

	private:
		friend TaskResult
		RunWithLoadingScreen(
			QWidget*,
			const QString&,
			const std::function<void(Progress&)>&,
			Cancellable);

		Progress(ProgressRelay* relay, assetlib::CancelToken cancel) :
			m_Relay(relay), m_Cancel(std::move(cancel))
		{}

		ProgressRelay*        m_Relay = nullptr;
		assetlib::CancelToken m_Cancel;
	};

	/**
	 * Runs `work` on a worker thread behind a modal loading screen, and returns once it finishes.
	 *
	 * `work` must touch only thread-safe state. Nothing in bgl qualifies -- Scene and ResourceManager
	 * carry no locks and the frame graph is documented single-threaded -- so a worker may decode and
	 * parse, but every scene mutation has to happen after this returns.
	 *
	 * Cancelling is cooperative: the screen stays up, modal, showing "Cancelling...", until `work`
	 * unwinds of its own accord, which for an assetlib cook is as long as the encode it is inside. It
	 * is also not a rollback -- whatever the cook had already written to disk is still there, and the
	 * caller owns cleaning that up.
	 *
	 * @return kCompleted when `work` ran to the end; kCancelled when the user pressed Cancel and `work`
	 *         threw assetlib::Cancelled in response; kFailed when it threw anything else, in which case
	 *         `TaskResult::error` carries the message to show.
	 */
	TaskResult
	RunWithLoadingScreen(
		QWidget*                              parent,
		const QString&                        title,
		const std::function<void(Progress&)>& work,
		Cancellable                           cancellable = Cancellable::kNo);
}
