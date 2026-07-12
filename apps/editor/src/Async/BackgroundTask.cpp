#include "Async/BackgroundTask.h"

#include <QCloseEvent>
#include <QEventLoop>
#include <QMetaObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QRunnable>
#include <QThreadPool>
#include <QWidget>

namespace background
{
	// Lives on the UI thread and is the only handle a worker has to the dialog.
	class ProgressRelay : public QObject
	{
	public:
		explicit ProgressRelay(QProgressDialog* dialog) : m_Dialog(dialog) {}

		void
		Apply(int done, int total, const QString& label)
		{
			// Once the user has asked to stop, the worker's remaining reports describe work that is
			// about to be thrown away. Letting them through would overwrite "Cancelling..." with a
			// label and a bar that both suggest the import is still going somewhere.
			if (m_Cancelling)
				return;

			if (!label.isEmpty())
				m_Dialog->setLabelText(label);

			// A zero range is QProgressDialog's busy indicator.
			m_Dialog->setRange(0, total);
			m_Dialog->setValue(done);
		}

		void
		ShowCancelling()
		{
			m_Cancelling = true;

			m_Dialog->setLabelText("Cancelling...");
			m_Dialog->setRange(0, 0);
			m_Dialog->setValue(0);

			// The work is going to be discarded, so there is nothing left to cancel a second time.
			if (auto* button = m_Dialog->findChild<QPushButton*>())
				button->setEnabled(false);
		}

	private:
		QProgressDialog* m_Dialog     = nullptr;
		bool             m_Cancelling = false;
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
				TaskResult*                    result) :
				m_Work(std::move(work)), m_Progress(std::move(progress)), m_Loop(loop),
				m_Result(result)
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
					// A throw that escapes QRunnable::run() takes the process down with it, so even a
					// type we know nothing about has to be turned into a message here.
					m_Result->status = TaskStatus::kFailed;
					m_Result->error  = QStringLiteral("an unknown error occurred");
				}

				// Queued, so quit() runs on the UI thread from inside the nested loop. Posting this
				// before exec() begins is safe: the event waits in the queue until exec() drains it.
				QMetaObject::invokeMethod(m_Loop, &QEventLoop::quit, Qt::QueuedConnection);
			}

		private:
			std::function<void(Progress&)> m_Work;
			Progress                       m_Progress;
			QEventLoop*                    m_Loop   = nullptr;
			TaskResult*                    m_Result = nullptr;
		};
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

		ProgressRelay relay(&dialog);
		QEventLoop    loop;
		TaskResult    result;

		auto source = std::stop_source();

		if (cancellable == Cancellable::kYes)
		{
			dialog.setCancelButtonText("Cancel");

			// QProgressDialog wires canceled() to its own cancel(), which hides the screen. We want the
			// opposite: keep it up, and ask the worker to stop.
			QObject::disconnect(
				&dialog,
				&QProgressDialog::canceled,
				&dialog,
				&QProgressDialog::cancel);

			QObject::connect(&dialog, &QProgressDialog::canceled, &dialog, [&]() {
				source.request_stop();
				relay.ShowCancelling();
			});
		}
		else
		{
			dialog.setCancelButton(nullptr);
		}

		dialog.setValue(0);

		// A private pool, so an import never queues behind a texture-preview decode. Its destructor
		// waits for the task, which has already finished by the time control reaches it.
		QThreadPool pool;
		pool.setMaxThreadCount(1);
		pool.start(new WorkTask(work, Progress(&relay, source.get_token()), &loop, &result));

		// Spins rather than blocks, so the DX12 viewports keep painting behind the dialog.
		loop.exec();

		return result;
	}
}
