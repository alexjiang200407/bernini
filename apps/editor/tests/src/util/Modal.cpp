#include "util/Modal.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QPushButton>
#include <QTimer>
#include <QtTest>

namespace editor::test
{
	namespace
	{
		/** How often the poll looks for the screen. Short enough not to dominate a test's runtime. */
		constexpr int c_PollMs = 5;
	}

	QProgressDialog*
	ActiveLoadingScreen()
	{
		// Deliberately not filtered on isVisible(): whether the screen is still up after Cancel is
		// something the tests assert, not something this may quietly decide for them.
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (auto* dialog = qobject_cast<QProgressDialog*>(widget); dialog != nullptr)
				return dialog;
		}

		return nullptr;
	}

	void
	OnLoadingScreen(std::function<bool(QProgressDialog&)> action)
	{
		// Parented to the application, so the timer belongs to the UI thread and survives the stack
		// frame that armed it -- it has to still be there when the nested loop starts running.
		auto* timer = new QTimer(qApp);
		timer->setInterval(c_PollMs);

		auto elapsed = std::make_shared<QElapsedTimer>();
		elapsed->start();

		// Whether the screen has ever been up. RunWithLoadingScreen constructs the dialog before it
		// shows it, so there is a window in which one exists but has never been on screen -- and acting
		// on it then would race the show, which is not a thing a user could have done.
		//
		// Latched rather than re-tested, because whether the screen stays up *after* the action is
		// exactly what some of the callers are here to find out.
		auto shown = std::make_shared<bool>(false);

		QObject::connect(
			timer,
			&QTimer::timeout,
			timer,
			[timer, elapsed, shown, action = std::move(action)]() {
				QProgressDialog* dialog = ActiveLoadingScreen();

				if (dialog != nullptr && dialog->isVisible())
					*shown = true;

				const bool done    = dialog != nullptr && *shown && action(*dialog);
				const bool expired = elapsed->hasExpired(c_ModalTimeoutMs);

				if (done || expired)
				{
					timer->stop();
					timer->deleteLater();
				}
			});

		timer->start();
	}

	void
	CancelLoadingScreen()
	{
		OnLoadingScreen([](QProgressDialog& dialog) {
			auto* cancel = dialog.findChild<QPushButton*>();
			if (cancel == nullptr)
				return false;

			QTest::mouseClick(cancel, Qt::LeftButton);
			return true;
		});
	}
}
