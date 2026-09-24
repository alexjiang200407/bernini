#include "Windows/GpuTiming/GpuTimingWindow.h"

#include "Windows/GpuTiming/PassGraphView.h"
#include "util/editor_language.h"
#include "util/held_open_assets.h"
#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <bgl/PassTiming.h>
#include <bgl/pass_timing_csv.h>
#include <qcontainerfwd.h>
#include <qlogging.h>
#include <qtmetamacros.h>
#include <vector>

namespace editor
{
	GpuTimingWindow::GpuTimingWindow(QWidget* parent) : QWidget(parent, Qt::Window)
	{
		setObjectName("GpuTimingWindow");
		setWindowTitle(Localize("editor.gpu_timing.window_title", "GPU Pass Timing"));
		resize(980, 420);

		m_Graph = new PassGraphView(m_History, this);

		m_Status = new QLabel(this);
		m_Status->setObjectName("GpuTimingStatus");

		m_Pause = new QPushButton(Localize("editor.gpu_timing.pause_button", "Pause"), this);
		m_Pause->setObjectName("GpuTimingPause");
		m_Pause->setToolTip(Localize(
			"editor.gpu_timing.pause_tooltip",
			"Stop recording, so a spike stays on screen instead of scrolling away."));

		auto* clear = new QPushButton(Localize("editor.gpu_timing.clear_button", "Clear"), this);
		clear->setObjectName("GpuTimingClear");

		auto* save =
			new QPushButton(Localize("editor.gpu_timing.export_button", "Export CSV…"), this);
		save->setObjectName("GpuTimingExport");
		save->setToolTip(Localize(
			"editor.gpu_timing.export_tooltip",
			"Write retained timings and current editor context beside editor.log, as CSV."));

		auto* controls = new QHBoxLayout();
		controls->addWidget(m_Status, 1);
		controls->addWidget(m_Pause);
		controls->addWidget(clear);
		controls->addWidget(save);

		auto* layout = new QVBoxLayout(this);
		layout->addLayout(controls);
		layout->addWidget(m_Graph, 1);

		connect(m_Pause, &QPushButton::clicked, this, [this] { SetPaused(!m_Paused); });
		connect(clear, &QPushButton::clicked, this, [this] {
			m_History.Clear();
			m_Graph->update();
			UpdateStatus();
		});
		connect(save, &QPushButton::clicked, this, [this] {
			// Beside the binary, which is where editor.log and any crash log are: an export nobody
			// can find is one nobody reads, and a file dialog cannot be driven by a test.
			const QDir    here(QCoreApplication::applicationDirPath());
			const QString file = Export(here);
			if (file.isEmpty())
			{
				qWarning() << "GPU pass timings: nothing to export";
				return;
			}

			qInfo() << "GPU pass timings written to" << here.filePath(file);
		});

		UpdateStatus();
	}

	void
	GpuTimingWindow::SetSource(const QString& viewport)
	{
		if (viewport == m_Source)
			return;

		m_Source = viewport;
		m_History.Clear();
		m_Graph->update();
		UpdateStatus();
	}

	void
	GpuTimingWindow::AddFrames(const std::vector<bgl::PassTimings>& frames)
	{
		if (m_Paused || frames.empty())
			return;

		for (const bgl::PassTimings& frame : frames)
		{
			m_History.Append(frame);
		}

		m_Graph->update();
		UpdateStatus();
	}

	QString
	GpuTimingWindow::Export(const QDir& directory)
	{
		if (m_History.SampleCount() == 0)
			return {};

		const QString file =
			"gpu_timings_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".csv";

		QFile csv(directory.filePath(file));
		if (!csv.open(QIODevice::WriteOnly | QIODevice::Text))
			return {};

		QStringList assets = GetAssetsHeldOpen(parentWidget());
		assets.removeDuplicates();
		assets.sort();
		const QJsonObject context{
			{ "snapshot", "export_time" },
			{ "exported_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) },
			{ "timing_tab", m_Source },
			{ "recording_paused", m_Paused },
			{ "editor_title", parentWidget() ? parentWidget()->windowTitle() : QString() },
			{ "open_assets_scope", "all_editor_panels" },
			{ "open_assets", QJsonArray::fromStringList(assets) }
		};
		QString metadata = QString::fromUtf8(QJsonDocument(context).toJson(QJsonDocument::Compact));
		metadata.replace('"', "\"\"");
		QTextStream stream(&csv);
		stream << "# export_context_json,\"" << metadata << "\"\n";
		stream << QString::fromStdString(bgl::PassHistoryCsv(m_History));
		stream.flush();
		csv.close();

		return file;
	}

	void
	GpuTimingWindow::showEvent(QShowEvent* event)
	{
		QWidget::showEvent(event);
		Q_EMIT TimingWanted(true);
	}

	void
	GpuTimingWindow::hideEvent(QHideEvent* event)
	{
		QWidget::hideEvent(event);
		Q_EMIT TimingWanted(false);
	}

	void
	GpuTimingWindow::SetPaused(const bool paused)
	{
		m_Paused = paused;
		m_Pause->setText(
			paused ? Localize("editor.gpu_timing.resume_button", "Resume") :
					 Localize("editor.gpu_timing.pause_button", "Pause"));
		UpdateStatus();
	}

	void
	GpuTimingWindow::UpdateStatus()
	{
		if (m_Source.isEmpty())
		{
			m_Status->setText(
				Localize("editor.gpu_timing.no_viewport", "No viewport is rendering."));
			return;
		}

		m_Status->setText(
			m_Paused ? Localize(
						   "editor.gpu_timing.status_paused",
						   { m_Source, m_History.SampleCount() },
						   "{0} — {1} frames, paused") :
					   Localize(
						   "editor.gpu_timing.status",
						   { m_Source, m_History.SampleCount() },
						   "{0} — {1} frames"));
	}
}
