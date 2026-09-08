#include "Windows/GpuTiming/GpuTimingWindow.h"

#include "Windows/GpuTiming/PassGraphView.h"
#include "Windows/GpuTiming/pass_graph_paint.h"
#include "Windows/GpuTiming/pass_timing_csv.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRect>
#include <QShowEvent>
#include <QString>
#include <QSvgGenerator>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <bgl/PassTiming.h>
#include <qlogging.h>
#include <qtmetamacros.h>
#include <vector>

namespace
{
	// The exported drawing is vector, so this is the shape the chart is laid out in rather than a
	// resolution -- the text metrics and band geometry are computed against it, and a reader zooms
	// as far into it as they like.
	constexpr int c_ExportWidth  = 1400;
	constexpr int c_ExportHeight = 560;
}

namespace editor
{
	GpuTimingWindow::GpuTimingWindow(QWidget* parent) : QWidget(parent, Qt::Window)
	{
		setObjectName("GpuTimingWindow");
		setWindowTitle("GPU Pass Timing");
		resize(980, 420);

		m_Graph = new PassGraphView(m_History, this);

		m_Status = new QLabel(this);
		m_Status->setObjectName("GpuTimingStatus");

		m_Pause = new QPushButton("Pause", this);
		m_Pause->setObjectName("GpuTimingPause");
		m_Pause->setToolTip(
			"Stop recording, so a spike stays on screen instead of scrolling away.");

		auto* clear = new QPushButton("Clear", this);
		clear->setObjectName("GpuTimingClear");

		auto* save = new QPushButton("Export…", this);
		save->setObjectName("GpuTimingExport");
		save->setToolTip("Write the graph and its numbers beside editor.log.");

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
			const QString stem = Export(here);
			if (stem.isEmpty())
			{
				qWarning() << "GPU pass timings: nothing to export";
				return;
			}

			qInfo() << "GPU pass timings written to" << here.filePath(stem) + ".{csv,svg}";
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

		const QString stem =
			"gpu_timings_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");

		QFile csv(directory.filePath(stem + ".csv"));
		if (!csv.open(QIODevice::WriteOnly | QIODevice::Text))
			return {};

		QTextStream(&csv) << PassHistoryCsv(m_History);
		csv.close();

		const QRect frame(0, 0, c_ExportWidth, c_ExportHeight);

		QSvgGenerator drawing;
		drawing.setFileName(directory.filePath(stem + ".svg"));
		drawing.setSize(frame.size());
		drawing.setViewBox(frame);
		drawing.setTitle("Bernini GPU pass timings");
		drawing.setDescription(QString("%1, %2 frames").arg(m_Source).arg(m_History.SampleCount()));

		{
			QPainter painter(&drawing);
			PaintPassGraph(painter, frame, m_History, m_Graph->Marked(), palette());
		}

		// QSvgGenerator reports nothing, so what says the drawing was written is the file.
		return QFileInfo(drawing.fileName()).size() > 0 ? stem : QString();
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
		m_Pause->setText(paused ? "Resume" : "Pause");
		UpdateStatus();
	}

	void
	GpuTimingWindow::UpdateStatus()
	{
		if (m_Source.isEmpty())
		{
			m_Status->setText("No viewport is rendering.");
			return;
		}

		m_Status->setText(QString("%1 — %2 frames%3")
		                      .arg(m_Source)
		                      .arg(m_History.SampleCount())
		                      .arg(m_Paused ? ", paused" : ""));
	}
}
