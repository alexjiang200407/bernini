#include "Async/BackgroundTask.h"

#include <QCloseEvent>
#include <QEventLoop>
#include <QMetaObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QRunnable>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>

namespace background
{
	// Lives on the UI thread and is the only handle a worker has to whatever is on screen.
	class ProgressRelay : public QObject
	{
	public:
		explicit ProgressRelay(ProgressSink sink) : m_Sink(std::move(sink)) {}

		void
		Apply(int done, int total, const QString& label)
		{
			// Once the user has asked to stop, the worker's remaining reports describe work that is
			// about to be thrown away. Letting them through would overwrite "Cancelling..." with a
			// label and a bar that both suggest the import is still going somewhere.
			if (m_Silenced)
				return;

			if (m_Sink)
				m_Sink(done, total, label);
		}

		void
		Silence()
		{
			m_Silenced = true;
		}

	private:
		ProgressSink m_Sink;
		bool         m_Silenced = false;
	};

	void
	Progress::Report(int done, int total, const QString& label)
	{
		if (m_Relay == nullptr)
			return;

		QMetaObject::invokeMethod(
			m_Relay,
			[relay = m_Relay, done, total, label]() { relay->Apply(done, total, label); },
			Qt::QueuedConnection);
	}

	assetlib::CancelToken
	Progress::Cancellation() const
	{
		return m_Cancel;
	}

	namespace
	{
		/**
		 * QProgressDialog dismisses itself the instant Cancel is pressed, and Esc or the title-bar X
		 * close it outright.
		 */
		class LoadingScreen : public QProgressDialog
		{
		public:
			using QProgressDialog::QProgressDialog;

			void
			reject() override
			{
				// Esc already reaches canceled() through the cancel button's shortcut; all QDialog's own
				// handling of it would add is closing the screen.
			}

		protected:
			void
			closeEvent(QCloseEvent* event) override
			{
				Q_EMIT canceled();
				event->ignore();
			}
		};

		class WorkTask : public QRunnable
		{
		public:
			WorkTask(
				std::function<void(Progress&)> work,
				Progress                       progress,
				QEventLoop*                    loop,
				TaskResult*                    result,
				std::atomic<bool>*             finished) :
				m_Work(std::move(work)), m_Progress(std::move(progress)), m_Loop(loop),
				m_Result(result), m_Finished(finished)
			{
				setAutoDelete(true);
			}

			void
			run() override
			{
				try
				{
					m_Work(m_Progress);
					m_Result->status = TaskStatus::kCompleted;
				}
				catch (const assetlib::Cancelled&)
				{
					// The user asked for this, so it is an outcome, not an error to report.
					m_Result->status = TaskStatus::kCancelled;
				}
				catch (const std::exception& e)
				{
					m_Result->status = TaskStatus::kFailed;
					m_Result->error  = QString::fromUtf8(e.what());
				}
				catch (...)
				{
					m_Result->status = TaskStatus::kFailed;
					m_Result->error  = QStringLiteral("an unknown error occurred");
				}

				// Written before the quit is posted, and read by the screen's poll: whichever of the two
				// reaches the UI thread first must see a result that is already complete.
				m_Finished->store(true, std::memory_order_release);

				// Queued, so quit() runs on the UI thread from inside the nested loop. Posting this
				// before exec() begins is safe: the event waits in the queue until exec() drains it.
				QMetaObject::invokeMethod(m_Loop, &QEventLoop::quit, Qt::QueuedConnection);
			}

		private:
			std::function<void(Progress&)> m_Work;
			Progress                       m_Progress;
			QEventLoop*                    m_Loop     = nullptr;
			TaskResult*                    m_Result   = nullptr;
			std::atomic<bool>*             m_Finished = nullptr;
		};
		/**
		 * `work` on a private pool thread, with the calling thread's event loop spinning until it
		 * finishes. Everything about a *screen* is the caller's; what is here is the wait.
		 *
		 * A private pool, so an import never queues behind a texture-preview decode.
		 */
		TaskResult
		RunPumped(
			const std::function<void(Progress&)>& work,
			Progress                              progress,
			ProgressRelay&                        relay)
		{
			QEventLoop loop;
			TaskResult result;

			// A nested loop entered from a platform callback -- a drop, which is the one that bites --
			// need not service Qt's posted-event source, so the worker's quit() may never arrive.
			// Timers are serviced there, so poll for completion as well; whichever lands first ends
			// the loop.
			auto finished = std::atomic<bool>(false);

			QTimer poll;
			poll.setInterval(30);
			QObject::connect(&poll, &QTimer::timeout, &relay, [&loop, &finished]() {
				if (finished.load(std::memory_order_acquire))
					loop.quit();
			});
			poll.start();

			// Declared last, so it is destroyed first: its destructor waits for the task, and `loop`,
			// `result` and `finished` must outlive that wait. Normally the task is long finished by
			// then, but QCoreApplication::exit() -- a session logoff -- exits every event loop on this
			// thread, including the nested one below, while the worker is still running. The wait is
			// what keeps it from writing into a dead stack frame.
			QThreadPool pool;
			pool.setMaxThreadCount(1);
			pool.start(new WorkTask(work, std::move(progress), &loop, &result, &finished));

			// Spins rather than blocks, so the viewports keep painting behind the screen.
			loop.exec();

			return result;
		}
	}

	TaskResult
	RunWithLoadingScreen(
		QWidget*                              parent,
		const QString&                        title,
		const std::function<void(Progress&)>& work,
		Cancellable                           cancellable)
	{
		QWidget* owner = parent != nullptr ? parent->window() : nullptr;

		LoadingScreen dialog(title, QString(), 0, 0, owner);
		dialog.setWindowTitle(title);
		dialog.setWindowModality(Qt::ApplicationModal);
		dialog.setAutoClose(false);
		dialog.setAutoReset(false);
		dialog.setMinimumDuration(0);

		ProgressRelay relay([&dialog](int done, int total, const QString& label) {
			if (!label.isEmpty())
				dialog.setLabelText(label);

			// A zero range is QProgressDialog's busy indicator.
			dialog.setRange(0, total);
			dialog.setValue(done);
		});

		auto source = std::stop_source();

		// QProgressDialog wires canceled() to its own cancel(), which hides the screen. We want the
		// opposite: keep it up until the worker unwinds. Disconnected for both kinds of work -- the X
		// emits canceled() whether or not there is a cancel button, and cancel() would hide the screen
		// out from under a worker that cannot be stopped, unblocking the app behind it.
		//
		// This has to be the SIGNAL/SLOT form. QProgressDialog makes that connection with the string
		// macros, which record a method index, whereas the pointer-to-member disconnect only matches
		// connections that recorded a slot object -- so it would find nothing, quietly return false,
		// and leave the screen free to dismiss itself the moment Cancel is pressed.
		const bool disconnected =
			QObject::disconnect(&dialog, SIGNAL(canceled()), &dialog, SLOT(cancel()));
		Q_ASSERT(disconnected);
		Q_UNUSED(disconnected);

		if (cancellable == Cancellable::kYes)
		{
			dialog.setCancelButtonText("Cancel");

			QObject::connect(&dialog, &QProgressDialog::canceled, &dialog, [&]() {
				source.request_stop();

				// Silenced first: the worker's remaining reports describe work about to be thrown
				// away, and would overwrite this with a label and a bar that both suggest the task
				// is still going somewhere.
				relay.Silence();

				dialog.setLabelText("Cancelling...");
				dialog.setRange(0, 0);
				dialog.setValue(0);

				// The work is going to be discarded, so there is nothing left to cancel a second time.
				if (auto* button = dialog.findChild<QPushButton*>())
					button->setEnabled(false);
			});
		}
		else
		{
			dialog.setCancelButton(nullptr);
		}

		dialog.setValue(0);

		return RunPumped(work, Progress(&relay, source.get_token()), relay);
	}

	TaskResult
	RunReporting(const ProgressSink& sink, const std::function<void(Progress&)>& work)
	{
		ProgressRelay relay(sink);
		return RunPumped(work, Progress(&relay, {}), relay);
	}
}
