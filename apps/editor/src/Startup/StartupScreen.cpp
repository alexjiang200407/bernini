#include "Startup/StartupScreen.h"

#include <QApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QScreen>
#include <QVBoxLayout>

namespace editor
{
	namespace
	{
		constexpr int c_Width = 420;
	}

	StartupScreen::StartupScreen(QString title) :
		QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
	{
		setAttribute(Qt::WA_ShowWithoutActivating);
		setFixedWidth(c_Width);

		auto* layout = new QVBoxLayout(this);
		layout->setContentsMargins(24, 20, 24, 20);
		layout->setSpacing(10);

		auto* name    = new QLabel(std::move(title), this);
		QFont heading = name->font();
		heading.setPointSize(heading.pointSize() + 4);
		heading.setBold(true);
		name->setFont(heading);
		layout->addWidget(name);

		m_Step = new QLabel(QStringLiteral("Starting..."), this);

		// The label carries file names and shader names, either of which can be longer than the
		// screen; eliding keeps a long one from widening it mid-startup, which reads as a flicker.
		m_Step->setTextFormat(Qt::PlainText);
		m_Step->setMinimumWidth(c_Width - 48);
		m_Step->setMaximumWidth(c_Width - 48);
		layout->addWidget(m_Step);

		m_Bar = new QProgressBar(this);
		m_Bar->setTextVisible(false);
		m_Bar->setRange(0, 0);
		layout->addWidget(m_Bar);

		if (const QScreen* screen = QApplication::primaryScreen())
		{
			adjustSize();
			move(screen->geometry().center() - rect().center());
		}
	}

	background::ProgressSink
	StartupScreen::Sink()
	{
		// Guarded rather than captured raw: the sink outlives this screen by design -- the project
		// half of startup keeps reporting through it after the window has taken over.
		return [self = QPointer<StartupScreen>(this)](int done, int total, const QString& label) {
			if (self.isNull())
				return;

			if (!label.isEmpty())
			{
				const QFontMetrics metrics(self->m_Step->font());
				self->m_Step->setText(
					metrics.elidedText(label, Qt::ElideMiddle, self->m_Step->maximumWidth()));
			}

			// A zero range is Qt's busy indicator, which is what a phase that cannot count reports.
			self->m_Bar->setRange(0, total);
			self->m_Bar->setValue(done);
		};
	}
}
