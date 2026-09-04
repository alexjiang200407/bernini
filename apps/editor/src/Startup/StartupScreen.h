#pragma once

#include <QWidget>
#include <qobject.h>

#include "Async/BackgroundTask.h"

class QLabel;
class QProgressBar;

namespace editor
{
	/**
	 * What is on screen while the editor starts: the one thing that can report the renderer being
	 * built, because the window that would otherwise report it is what is being built.
	 *
	 * Frameless and centred rather than a QSplashScreen: a splash paints its message itself and
	 * has no bar, and what this shows is a determinate count of pipelines and then of assets.
	 *
	 * Not modal and not a dialog. It is up before there is a window for a modal to be modal over,
	 * and it offers nothing to press -- there is no sensible thing to do with a half-built editor.
	 */
	class StartupScreen : public QWidget
	{
	public:
		explicit StartupScreen(QString title);

		/** Where the work reports. Safe to hold past this screen; a closed screen ignores it. */
		[[nodiscard]] background::ProgressSink
		Sink();

	private:
		QLabel*       m_Step = nullptr;
		QProgressBar* m_Bar  = nullptr;
	};
}
