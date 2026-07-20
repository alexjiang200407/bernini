#include "Windows/RenderTarget/RenderTargetWindow.h"

#include "Render/Renderer.h"

#include <QDebug>
#include <QHideEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <bgl/IGraphics.h>
#include <bgl/RenderContext.h>
#include <bgl/Viewport.h>

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

	auto rtvDesc   = bgl::RenderTargetDesc();
	rtvDesc.width  = m_Width;
	rtvDesc.height = m_Height;
	// winId() must be resolved here on the GUI thread; the render target is created from the value.
	rtvDesc.wnd      = reinterpret_cast<void*>(winId());
	rtvDesc.headless = false;

	m_RenderTarget = m_Desc.renderer->Invoke(
		[&] { return m_Desc.renderer->GetGraphics()->CreateRenderTarget(rtvDesc); });
	m_SceneView = m_Desc.renderer->Invoke([&] {
		return m_Desc.renderer->GetGraphics()->CreateSceneView(
			m_Desc.renderer->GetScene(),
			m_Desc.maxInstances);
	});

	m_RenderWidth  = m_Width;
	m_RenderHeight = m_Height;

	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);

	// Deliberately not SyncSize'd here: the frame loop must not chase the window's size, or it would
	// resize the backbuffers on the very next frame and undo the settle timer above.
	m_FrameClock.start();
}

RenderTargetWindow::~RenderTargetWindow()
{
	if (m_Desc.renderer == nullptr)
		return;

	if (m_ViewportId != 0)
		m_Desc.renderer->RemoveViewport(m_ViewportId);

	// A blocking round-trip, so every frame and every camera update posted for this window has already
	// run by the time it returns: none can outlive the widget.
	m_Desc.renderer->Invoke([&] {
		m_SceneView    = nullptr;
		m_RenderTarget = nullptr;
	});
}

void
RenderTargetWindow::DrawFrame()
{
	auto rc     = bgl::RenderContext();
	rc.camera   = m_RenderCamera;
	rc.view     = m_SceneView;
	rc.viewport = bgl::Viewport(m_RenderWidth, m_RenderHeight);

	m_Desc.renderer->GetGraphics()->DrawFrame(m_RenderTarget, rc);
}

void
RenderTargetWindow::SetCamera(const bgl::Camera& cam)
{
	m_Desc.renderer->Post([this, cam] { m_RenderCamera = cam; });
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

		m_FrameTimes.Push(deltaMs);

		if (deltaMs > c_MissedFrameMs)
		{
			++m_MissedFrames;
			qWarning(
				"RenderTarget: missed a vblank -- frame %.1f ms (draw %.1f ms, gap %.1f ms)",
				deltaMs,
				drawMs,
				gapMs);
		}

		if (++m_FramesSinceEmit >= c_FrameStatsInterval)
		{
			m_FramesSinceEmit = 0;
			Q_EMIT FrameStatsUpdated(
				m_FrameTimes.Mean(),
				m_FrameTimes.Max(),
				static_cast<int>(m_MissedFrames));
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

	if (m_ViewportId == 0)
	{
		m_ViewportId = m_Desc.renderer->AddViewport([this]() {
			const qint64 startNs = m_FrameClock.nsecsElapsed();
			DrawFrame();
			const qint64 endNs = m_FrameClock.nsecsElapsed();

			ReportFrameTiming(startNs, endNs);
		});
	}
}

void
RenderTargetWindow::hideEvent(QHideEvent* event)
{
	if (m_ViewportId != 0)
	{
		m_Desc.renderer->RemoveViewport(m_ViewportId);
		m_ViewportId = 0;
	}

	// RemoveViewport blocks until the frame loop has dropped this window, so no frame is in flight
	// and the render thread cannot be mid-ReportFrameTiming. Clearing the timestamps keeps the time
	// spent hidden from being measured as one enormous frame when the window comes back.
	m_LastFrameStartNs = -1;
	m_LastFrameEndNs   = -1;

	// Nothing is presenting while hidden, so a pending resize has nothing to serve; showEvent takes
	// the size again anyway.
	m_ResizeTimer->stop();

	QWidget::hideEvent(event);
}

void
RenderTargetWindow::SyncSize(int w, int h)
{
	if (m_RenderTarget == nullptr || m_Desc.renderer == nullptr)
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

	// Blocking, so the size the frame loop reads changes between frames and never mid-frame.
	m_Desc.renderer->Invoke([&] {
		m_Desc.renderer->GetGraphics()->Resize(m_RenderTarget, width, height);
		m_RenderWidth  = width;
		m_RenderHeight = height;
	});

	qWarning(
		"RenderTarget: resized to %ux%u in %.1f ms",
		width,
		height,
		static_cast<double>(resizeClock.nsecsElapsed()) * 1.0e-6);
}
