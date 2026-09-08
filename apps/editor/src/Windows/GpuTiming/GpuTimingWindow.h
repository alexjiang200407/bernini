#pragma once

#include <QString>
#include <QWidget>
#include <bgl/PassTiming.h>
#include <qtmetamacros.h>
#include <vector>

#include "Windows/GpuTiming/PassHistory.h"

class QDir;
class QLabel;
class QPushButton;
class QHideEvent;
class QShowEvent;

namespace editor
{
	class PassGraphView;

	/**
	 * The per-pass GPU cost of the rendering viewport, over the last few seconds.
	 *
	 * A **top-level window** rather than a dock: MainWindow keeps a viewport in the frame loop only
	 * while its dock is the selected tab, so a graph docked among them would stop the viewport it
	 * measures the moment it was brought forward.
	 *
	 * Timing costs a resolve per frame, so the window asks for it while it is on screen (TimingWanted)
	 * rather than assuming somebody switched it on first.
	 */
	class GpuTimingWindow : public QWidget
	{
		Q_OBJECT

	public:
		explicit GpuTimingWindow(QWidget* parent = nullptr);

		/**
		 * Names the viewport the frames are arriving from; a different one clears what is held,
		 * since one viewport's frames say nothing about another's. Empty means none is rendering.
		 */
		void
		SetSource(const QString& viewport);

		/** Records timed frames, oldest first. Ignored while paused. */
		void
		AddFrames(const std::vector<bgl::PassTimings>& frames);

		/**
		 * Writes the history into `directory` as `gpu_timings_<stamp>.csv` and `.svg` -- the numbers
		 * for a spreadsheet or an agent, and the graph as it stands for a person. Vector, so a
		 * two-pixel spike among six hundred frames is something a reader can zoom into.
		 *
		 * @return the stem both files share, or an empty string when nothing was recorded or a file
		 *         could not be written.
		 */
		[[nodiscard]] QString
		Export(const QDir& directory);

		[[nodiscard]] const PassHistory&
		History() const noexcept
		{
			return m_History;
		}

		[[nodiscard]] bool
		IsPaused() const noexcept
		{
			return m_Paused;
		}

	Q_SIGNALS:
		/** Raised while the window is on screen, so somebody turns per-pass timing on for it. */
		void
		TimingWanted(bool wanted);

	protected:
		void
		showEvent(QShowEvent* event) override;

		void
		hideEvent(QHideEvent* event) override;

	private:
		void
		SetPaused(bool paused);

		void
		UpdateStatus();

		PassHistory    m_History;
		PassGraphView* m_Graph  = nullptr;
		QLabel*        m_Status = nullptr;
		QPushButton*   m_Pause  = nullptr;
		QString        m_Source;
		bool           m_Paused = false;
	};
}
