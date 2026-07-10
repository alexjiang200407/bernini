#include "Async/BackgroundTask.h"

#include <QEventLoop>
#include <QMetaObject>
#include <QProgressDialog>
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
			if (!label.isEmpty())
				m_Dialog->setLabelText(label);

			// A zero range is QProgressDialog's busy indicator.
			m_Dialog->setRange(0, total);
			m_Dialog->setValue(done);
		}

	private:
		QProgressDialog* m_Dialog = nullptr;
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

	namespace
	{
		class WorkTask : public QRunnable
		{
		public:
			WorkTask(
				std::function<void(Progress&)> work,
				Progress                       progress,
				QEventLoop*                    loop,
				QString*                       error,
				bool*                          ok) :
				m_Work(std::move(work)), m_Progress(progress), m_Loop(loop), m_Error(error),
				m_Ok(ok)
			{
				setAutoDelete(true);
			}

			void
			run() override
			{
				try
				{
					m_Work(m_Progress);
					*m_Ok = true;
				}
				catch (const std::exception& e)
				{
					*m_Error = QString::fromUtf8(e.what());
				}

				// Queued, so quit() runs on the UI thread from inside the nested loop. Posting this
				// before exec() begins is safe: the event waits in the queue until exec() drains it.
				QMetaObject::invokeMethod(m_Loop, &QEventLoop::quit, Qt::QueuedConnection);
			}

		private:
			std::function<void(Progress&)> m_Work;
			Progress                       m_Progress;
			QEventLoop*                    m_Loop  = nullptr;
			QString*                       m_Error = nullptr;
			bool*                          m_Ok    = nullptr;
		};
	}

	bool
	RunWithLoadingScreen(
		QWidget*                              parent,
		const QString&                        title,
		const std::function<void(Progress&)>& work,
		QString*                              error)
	{
		QWidget* owner = parent != nullptr ? parent->window() : nullptr;

		QProgressDialog dialog(title, QString(), 0, 0, owner);
		dialog.setWindowTitle(title);
		dialog.setWindowModality(Qt::ApplicationModal);
		dialog.setCancelButton(nullptr);  // assetlib exposes no cancellation hooks
		dialog.setAutoClose(false);
		dialog.setAutoReset(false);
		dialog.setMinimumDuration(0);
		dialog.setValue(0);

		ProgressRelay relay(&dialog);
		QEventLoop    loop;
		QString       message;
		bool          ok = false;

		// A private pool, so an import never queues behind a texture-preview decode. Its destructor
		// waits for the task, which has already finished by the time control reaches it.
		QThreadPool pool;
		pool.setMaxThreadCount(1);
		pool.start(new WorkTask(work, Progress(&relay), &loop, &message, &ok));

		// Spins rather than blocks, so the DX12 viewports keep painting behind the dialog.
		loop.exec();

		if (!ok && error != nullptr)
			*error = message;

		return ok;
	}
}
