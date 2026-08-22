#include "Windows/RenderTarget/RenderTargetWindow.h"

#include "Render/Renderer.h"

#if defined(__APPLE__)
#	include "Platform/MetalSurface.h"
#endif

#include <QDebug>
#include <QHideEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <bgl/IGraphics.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>

namespace
{
	constexpr int c_ResizeSettleMs = 200;

	// Below this a viewport is a handful of pixels and above it costs more than any display can
	// show: either is a typo in config.json rather than an intent.
	constexpr float c_MinRenderScale = 0.1f;
	constexpr float c_MaxRenderScale = 4.0f;

	// Qt sizes are logical (device-independent) pixels; the render target and the layer's drawable
	// are physical ones. Rendering at the logical size leaves the compositor upscaling every frame
	// on a high-DPI display -- half resolution on a 2x screen, which reads as blur everywhere and
	// is worst on detail near the pixel scale.
	//
	// The render scale is not folded in here: this is the size the target *presents* at, and the
	// grid the geometry passes draw on is the target's own business now.
	uint32_t
	GetPhysicalExtent(int logical, qreal ratio)
	{
		return std::max<uint32_t>(
			1,
			static_cast<uint32_t>(std::lround(std::max(1, logical) * ratio)));
	}

	float
	ClampRenderScale(float scale)
	{
		const float clamped = std::clamp(scale, c_MinRenderScale, c_MaxRenderScale);
		if (clamped != scale)
			qWarning("RenderTarget: render scale %.3f out of range, using %.3f", scale, clamped);

		return clamped;
	}

	// Below a fifth of a pixel the kernel starves every phase but the one that lands dead centre and
	// the accumulation stops converging; above two it spans neighbours the reconstruction exists to
	// keep apart. Either is a typo in config.json rather than an intent.
	constexpr float c_MinReconstructionWidth = 0.1f;
	constexpr float c_MaxReconstructionWidth = 2.0f;

	float
	ClampReconstructionWidth(float width)
	{
		const float clamped = std::clamp(width, c_MinReconstructionWidth, c_MaxReconstructionWidth);
		if (clamped != width)
		{
			qWarning(
				"RenderTarget: TAA reconstruction width %.3f out of range, using %.3f",
				static_cast<double>(width),
				static_cast<double>(clamped));
		}

		return clamped;
	}
}

RenderTargetWindow::RenderTargetWindow(QWidget* parent, RenderTargetWindowDesc desc) :
	QWidget(parent), m_Desc(std::move(desc))
{
	m_ResizeTimer = new QTimer(this);
	m_ResizeTimer->setSingleShot(true);
	connect(m_ResizeTimer, &QTimer::timeout, this, [this]() { SyncSize(width(), height()); });

	m_RenderScale            = ClampRenderScale(m_Desc.renderScale);
	m_TaaReconstructionWidth = ClampReconstructionWidth(m_Desc.taaReconstructionWidth);

	m_Width  = GetPhysicalExtent(width(), devicePixelRatio());
	m_Height = GetPhysicalExtent(height(), devicePixelRatio());

	auto rtvDesc                   = bgl::RenderTargetDesc();
	rtvDesc.width                  = m_Width;
	rtvDesc.height                 = m_Height;
	rtvDesc.renderScale            = m_RenderScale;
	rtvDesc.taaReconstructionWidth = m_TaaReconstructionWidth;
	// Resolved here on the GUI thread; the render target is created from the value on the render
	// thread. macOS takes the widget's CAMetalLayer rather than its native view -- see
	// platform::MetalLayerForView.
#if defined(__APPLE__)
	rtvDesc.wnd = platform::MetalLayerForView(winId());
#else
	rtvDesc.wnd = reinterpret_cast<void*>(winId());
#endif
	rtvDesc.headless = false;

	// A viewport redraws continuously, so convergence is free. The thumbnail cache cannot redraw
	// its way there, so it draws hashed alpha as the blend it converges to instead.
	rtvDesc.taaEnabled = m_Desc.taaEnabled;

	m_RenderTarget = m_Desc.renderer->Invoke(
		[&] { return m_Desc.renderer->GetGraphics()->CreateRenderTarget(rtvDesc); });
	m_SceneView = m_Desc.renderer->Invoke([&] {
		return m_Desc.renderer->GetGraphics()->CreateSceneView(
			m_Desc.renderer->GetScene(),
			m_Desc.initialInstances);
	});

	m_DrawWidth  = m_Width;
	m_DrawHeight = m_Height;

	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);

	// The density every temporal artifact is judged at, and the one thing about a viewport that a
	// screenshot cannot be read back from -- a capture is the output size whatever the scale.
	// SyncSize reports each later change, this reports the first.
	qWarning(
		"RenderTarget: created %ux%u, rendering %ux%u (scale %.2f, device pixel ratio %.2f)",
		m_Width,
		m_Height,
		m_RenderTarget->GetRenderWidth(),
		m_RenderTarget->GetRenderHeight(),
		static_cast<double>(m_RenderScale),
		static_cast<double>(devicePixelRatio()));

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
	auto job     = bgl::RenderJob();
	job.camera   = m_RenderCamera;
	job.time     = m_RenderTime;
	job.view     = m_SceneView;
	job.viewport = bgl::Viewport(m_DrawWidth, m_DrawHeight);

	m_Desc.renderer->GetGraphics()->DrawFrame(m_RenderTarget, job);
}

void
RenderTargetWindow::SetCamera(const bgl::Camera& cam)
{
	m_Desc.renderer->Post([this, cam] { m_RenderCamera = cam; });
}

void
RenderTargetWindow::SetTime(const float seconds)
{
	m_Desc.renderer->Post([this, seconds] { m_RenderTime = seconds; });
}

void
RenderTargetWindow::ReportFrameTiming(qint64 startNs)
{
	// Present is vsync-locked, so a healthy frame lands on one refresh (~16.7ms). Treat anything
	// past ~1.2 refreshes as having missed a vblank.
	constexpr double c_MissedFrameMs = 20.0;
	constexpr double c_NsToMs        = 1.0e-6;

	if (m_LastFrameStartNs >= 0)
	{
		const double deltaMs = static_cast<double>(startNs - m_LastFrameStartNs) * c_NsToMs;

		m_FrameTimes.Push(deltaMs);

		if (deltaMs > c_MissedFrameMs)
			++m_MissedFrames;

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
}

void
RenderTargetWindow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	m_ResizeTimer->start(c_ResizeSettleMs);
}

bool
RenderTargetWindow::event(QEvent* e)
{
	// Dragging the window to a screen with a different scale changes the physical size without a
	// resize; SyncSize recomputes from the ratio, so it only needs the nudge.
	if (e->type() == QEvent::DevicePixelRatioChange)
	{
		m_ResizeTimer->start(c_ResizeSettleMs);
	}
	return QWidget::event(e);
}

void
RenderTargetWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);

	m_ResizeTimer->stop();
	SyncSize(width(), height());

	UpdateViewport();
}

void
RenderTargetWindow::hideEvent(QHideEvent* event)
{
	UpdateViewport();

	// Nothing is presenting while hidden, so a pending resize has nothing to serve; showEvent takes
	// the size again anyway.
	m_ResizeTimer->stop();

	QWidget::hideEvent(event);
}

void
RenderTargetWindow::SetTaaEnabled(bool enabled)
{
	if (m_RenderTarget == nullptr || m_Desc.renderer == nullptr)
		return;

	// This viewport allocated no history, so there is nothing to switch and SetTaaEnabled(true)
	// would throw. The menu offers one entry across viewports that need not agree.
	if (!m_Desc.taaEnabled)
		return;

	// The render thread is what reads this between frames, so the change is posted there rather
	// than written from the GUI thread under it.
	m_Desc.renderer->Invoke([&] { m_RenderTarget->SetTaaEnabled(enabled); });
}

void
RenderTargetWindow::SetOutlineEnabled(bool enabled)
{
	if (m_RenderTarget == nullptr || m_Desc.renderer == nullptr)
		return;

	m_Desc.renderer->Invoke([&] { m_RenderTarget->SetOutlineEnabled(enabled); });
}

void
RenderTargetWindow::SetRenderScale(float scale)
{
	if (m_RenderTarget == nullptr || m_Desc.renderer == nullptr)
		return;

	const float clamped = ClampRenderScale(scale);
	if (clamped == m_RenderScale)
		return;

	m_RenderScale = clamped;

	// Only the geometry passes' grid moves, so the swapchain, the backbuffers and the frame ring
	// stay: this is not a resize and must not go through the settle timer that batches one.
	m_Desc.renderer->Invoke(
		[&] { m_Desc.renderer->GetGraphics()->SetRenderScale(m_RenderTarget, m_RenderScale); });

	qWarning(
		"RenderTarget: render scale %.2f, rendering %ux%u into %ux%u",
		static_cast<double>(m_RenderScale),
		m_RenderTarget->GetRenderWidth(),
		m_RenderTarget->GetRenderHeight(),
		m_Width,
		m_Height);
}

void
RenderTargetWindow::SetTaaReconstructionWidth(float width)
{
	if (m_RenderTarget == nullptr || m_Desc.renderer == nullptr)
		return;

	const float clamped = ClampReconstructionWidth(width);
	if (clamped == m_TaaReconstructionWidth)
		return;

	m_TaaReconstructionWidth = clamped;

	// A per-frame shader constant: nothing is reallocated, the accumulation is kept, and the next
	// frame reconstructs with it. Posted to the render thread all the same, because that is what
	// reads it between frames.
	m_Desc.renderer->Invoke(
		[&] { m_RenderTarget->SetTaaReconstructionWidth(m_TaaReconstructionWidth); });
}

void
RenderTargetWindow::SetRenderingEnabled(bool enabled)
{
	m_RenderingEnabled = enabled;
	UpdateViewport();
}

void
RenderTargetWindow::UpdateViewport()
{
	const bool render = m_RenderingEnabled && isVisible();
	if (render == (m_ViewportId != 0))
		return;

	if (render)
	{
		m_ViewportId = m_Desc.renderer->AddViewport([this]() {
			const qint64 startNs = m_FrameClock.nsecsElapsed();
			DrawFrame();
			ReportFrameTiming(startNs);
		});
		return;
	}

	m_Desc.renderer->RemoveViewport(m_ViewportId);
	m_ViewportId = 0;

	// RemoveViewport blocks until the frame loop has dropped this window, so no frame is in flight
	// and the render thread cannot be mid-ReportFrameTiming. Clearing the timestamp keeps the time
	// spent out of the loop from being measured as one enormous frame when the window rejoins, and
	// the rest goes with it so a window coming back reports what it is doing now rather than
	// averaging in whatever it was doing before it was put away.
	m_LastFrameStartNs = -1;
	m_FrameTimes.Reset();
	m_MissedFrames    = 0;
	m_FramesSinceEmit = 0;
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

	const uint32_t width  = GetPhysicalExtent(w, devicePixelRatio());
	const uint32_t height = GetPhysicalExtent(h, devicePixelRatio());
	if (width == m_Width && height == m_Height)
	{
		return;
	}

	m_Width  = width;
	m_Height = height;

	// Resize idles the whole GPU (RenderContext::Resize -> CommandQueue::Flush) before it can release
	// the backbuffers, so it costs a full pipeline drain. Time it and say so: with the settle timer
	// there should be exactly one of these per drag, and if there is more than one, the timer is not
	// doing its job.
	QElapsedTimer resizeClock;
	resizeClock.start();

	// Blocking, so the size the frame loop reads changes between frames and never mid-frame.
	m_Desc.renderer->Invoke([&] {
		m_Desc.renderer->GetGraphics()->Resize(m_RenderTarget, width, height);
		m_DrawWidth  = width;
		m_DrawHeight = height;
	});

	qWarning(
		"RenderTarget: resized to %ux%u, rendering %ux%u, in %.1f ms",
		width,
		height,
		m_RenderTarget->GetRenderWidth(),
		m_RenderTarget->GetRenderHeight(),
		static_cast<double>(resizeClock.nsecsElapsed()) * 1.0e-6);
}
