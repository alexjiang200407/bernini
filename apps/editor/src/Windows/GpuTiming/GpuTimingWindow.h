#pragma once

#include <QString>
#include <QWidget>
#include <bgl/PassTiming.h>
#include <cstddef>
#include <qtmetamacros.h>
#include <vector>

#include <bgl/PassHistory.h>

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
		 * Writes all retained samples, up to 3,600, into `directory` as `gpu_timings_<stamp>.csv`.
		 * The graph displays only the latest 600 of these samples.
		 * A context record precedes the timing table, describing the editor at export time.
		 *
		 * @return the file written, or an empty string when nothing was recorded or it could not be
		 *         written.
		 */
		[[nodiscard]] QString
		Export(const QDir& directory);

		[[nodiscard]] const bgl::PassHistory&
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

		static constexpr std::size_t c_CaptureCapacity = 3600;

		bgl::PassHistory m_History{ c_CaptureCapacity };
		PassGraphView*   m_Graph  = nullptr;
		QLabel*          m_Status = nullptr;
		QPushButton*     m_Pause  = nullptr;
		QString          m_Source;
		bool             m_Paused = false;
	};
}
