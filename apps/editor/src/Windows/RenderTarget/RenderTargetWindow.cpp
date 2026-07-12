#include "Windows/RenderTarget/RenderTargetWindow.h"
#include <QDebug>
#include <QHideEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <bgl/IGraphics.h>

namespace
{
	constexpr int c_ResizeSettleMs = 200;
}

RenderTargetWindow::RenderTargetWindow(QWidget* parent, RenderTargetWindowDesc desc) :
	QWidget(parent), m_Desc(std::move(desc))
{
	m_ResizeTimer = new QTimer(this);
	m_ResizeTimer->setSingleShot(true);
	connect(m_ResizeTimer, &QTimer::timeout, this, [this]() { SyncSize(width(), height()); });

	m_Width  = static_cast<uint32_t>(std::max(1, width()));
	m_Height = static_cast<uint32_t>(std::max(1, height()));

	auto rtvDesc     = bgl::RenderTargetDesc();
	rtvDesc.width    = m_Width;
	rtvDesc.height   = m_Height;
	rtvDesc.wnd      = reinterpret_cast<void*>(winId());
	rtvDesc.headless = false;

	m_RenderTarget = m_Desc.gfx->CreateRenderTarget(rtvDesc);
	m_SceneView    = m_Desc.gfx->CreateSceneView(m_Desc.scene, m_Desc.maxInstances);

	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);

	m_FrameTimer = new QTimer(this);
	// Present is vsync-locked (~16.67ms), and this 16ms timer sits right at that boundary. A coarse
	// timer (the default) jitters by the OS granularity (~15.6ms after a resize resets the timer
	// resolution), so it occasionally fires just after the vsync deadline, misses a refresh and waits
	// for the next -- averaging to ~23ms and staying there. A precise timer forces 1ms OS resolution,
	// so it reliably fires before vsync and the cadence stays at one refresh.
	m_FrameTimer->setTimerType(Qt::PreciseTimer);

	// Deliberately not SyncSize'd here: the frame loop must not chase the window's size, or it would
	// resize the backbuffers on the very next frame and undo the settle timer below.
	m_FrameClock.start();
	connect(m_FrameTimer, &QTimer::timeout, this, [this]() {
		const qint64 startNs = m_FrameClock.nsecsElapsed();
		DrawFrame(m_Desc.gfx.Get());
		const qint64 endNs = m_FrameClock.nsecsElapsed();

		ReportFrameTiming(startNs, endNs);
	});
}

void
RenderTargetWindow::ReportFrameTiming(qint64 startNs, qint64 endNs)
{
	// Present is vsync-locked, so a healthy frame lands on one refresh (~16.7ms). Treat anything
	// past ~1.2 refreshes as having missed a vblank and report what caused it.
	constexpr double c_MissedFrameMs = 20.0;
	constexpr double c_NsToMs        = 1.0e-6;

	if (m_LastFrameStartNs >= 0)
	{
		const double deltaMs = static_cast<double>(startNs - m_LastFrameStartNs) * c_NsToMs;
		const double drawMs  = static_cast<double>(endNs - startNs) * c_NsToMs;
		const double gapMs   = static_cast<double>(startNs - m_LastFrameEndNs) * c_NsToMs;

		if (deltaMs > c_MissedFrameMs)
		{
			qWarning(
				"RenderTarget: missed a vblank -- frame %.1f ms (draw %.1f ms, gap %.1f ms)",
				deltaMs,
				drawMs,
				gapMs);
		}
	}

	m_LastFrameStartNs = startNs;
	m_LastFrameEndNs   = endNs;
}

void
RenderTargetWindow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	m_ResizeTimer->start(c_ResizeSettleMs);
}

void
RenderTargetWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);

	m_ResizeTimer->stop();
	SyncSize(width(), height());

	// The frame rate is paced by the vsync-locked Present, not by this timer: DXGI queues frames and
	// Present only blocks once that queue is full, at which point the loop settles onto the refresh
	// rate. So the timer's only job is to re-drive the loop as soon as the previous frame returns.
	//
	// It must be a *zero-interval* timer, which fires whenever the event loop goes idle. Any non-zero
	// interval is quantised to the Windows timer tick (~15.6ms), which made the timer -- not Present
	// -- the pacer: it intermittently skipped a tick and stretched a frame to ~33ms (PIX smooths that
	// mixture into the ~22ms it reports).
	m_FrameTimer->start(0);
}

void
RenderTargetWindow::hideEvent(QHideEvent* event)
{
	m_FrameTimer->stop();

	// Nothing is presenting while hidden, so a pending resize has nothing to serve; showEvent takes
	// the size again anyway.
	m_ResizeTimer->stop();

	QWidget::hideEvent(event);
}

void
RenderTargetWindow::SyncSize(int w, int h)
{
	if (m_RenderTarget == nullptr || m_Desc.gfx == nullptr)
	{
		return;
	}

	// Zero dimensions (e.g. while minimised) would be rejected by Resize.
	if (w <= 0 || h <= 0)
	{
		return;
	}

	const uint32_t width  = static_cast<uint32_t>(w);
	const uint32_t height = static_cast<uint32_t>(h);
	if (width == m_Width && height == m_Height)
	{
		return;
	}

	m_Width  = width;
	m_Height = height;

	// Resize idles the whole GPU (Graphics::Resize -> CommandQueue::Flush) before it can release the
	// backbuffers, so it costs a full pipeline drain. Time it and say so: with the settle timer there
	// should be exactly one of these per drag, and if there is more than one, the timer is not doing
	// its job.
	QElapsedTimer resizeClock;
	resizeClock.start();
	m_Desc.gfx->Resize(m_RenderTarget, width, height);

	qWarning(
		"RenderTarget: resized to %ux%u in %.1f ms",
		width,
		height,
		static_cast<double>(resizeClock.nsecsElapsed()) * 1.0e-6);
}
