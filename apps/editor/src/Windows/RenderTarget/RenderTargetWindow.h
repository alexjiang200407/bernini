#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QTimer;

#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <core/stats/RollingWindow.h>

#include "Render/Renderer.h"

struct RenderTargetWindowDesc
{
	Renderer* renderer         = nullptr;
	uint32_t  initialInstances = 0;

	// Whether this viewport allocates temporal-AA resources and starts with it running. False also
	// frees the history buffers and their RTVs, which a runtime toggle cannot.
	bool taaEnabled = true;

	// How dense the grid the geometry passes draw on is, relative to the window's own resolution.
	// Below 1 the frame is rendered smaller and the TAA resolve reconstructs the window's resolution
	// back out of it, which puts a viewport on another display's pixel density -- a
	// resolution-dependent temporal artifact can then be reproduced on hardware that does not have
	// that display. Clamped to [0.1, 4].
	float renderScale = 1.0f;
};

class RenderTargetWindow : public QWidget
{
	Q_OBJECT

public:
	explicit RenderTargetWindow(QWidget* parent = nullptr, RenderTargetWindowDesc desc = {});
	~RenderTargetWindow() override;

	/**
	 * Keeps this window out of the frame loop however visible Qt considers it. A tabified
	 * QDockWidget does not hide the widget whose tab is unselected, so visibility alone would leave
	 * an unseen viewport rendering every frame; MainWindow drives this from
	 * QDockWidget::visibilityChanged.
	 */
	void
	SetRenderingEnabled(bool enabled);

	// Turns temporal AA on or off for this viewport, so it can be compared against itself without
	// restarting the editor. A no-op on a viewport configured without it -- there is no history to
	// switch on, and the setting that says so is in config.json rather than here.
	void
	SetTaaEnabled(bool enabled);

	// Turns the selection outline on or off for this viewport. Selection state is untouched, so
	// re-enabling shows the current selection again.
	void
	SetOutlineEnabled(bool enabled);

	// Whether this viewport allocated temporal-AA resources, and so has anything to toggle.
	[[nodiscard]] bool
	IsTaaAvailable() const noexcept
	{
		return m_Desc.taaEnabled;
	}

	// Rescales the grid the geometry passes draw on against the window the target fills, so one
	// display can be driven at another's pixel density without leaving the editor running. What is
	// presented and what a screenshot captures do not move. Out-of-range values are clamped and
	// warned about rather than rejected: this arrives from config.json as often as from the menu.
	void
	SetRenderScale(float scale);

	// Hands the animation clock to the render thread, as SetCamera hands the camera: what this
	// window's frames carry as RenderJob::time. Public because the clock is the *panel's* policy,
	// not the viewport's -- the Animation panel's transport drives its preview from outside. A
	// window nobody clocks draws at time zero, which freezes VAT instances on their phase.
	void
	SetTime(float seconds);

	[[nodiscard]] float
	GetRenderScale() const noexcept
	{
		return m_RenderScale;
	}

protected:
	void
	resizeEvent(QResizeEvent* event) override;

	bool
	event(QEvent* e) override;

	void
	showEvent(QShowEvent* event) override;
	void
	hideEvent(QHideEvent* event) override;

	// This widget hosts an external DX12 swapchain and presents itself every frame, so
	// Qt must not paint the surface; returning nullptr disables Qt's own painting.
	QPaintEngine*
	paintEngine() const override
	{
		return nullptr;
	}

	// The shared Scene. Only valid to touch on the render thread, i.e. inside a Post/Invoke closure.
	bgl::IScene*
	GetPreviewScene() const noexcept
	{
		return m_Desc.renderer != nullptr ? m_Desc.renderer->GetScene().Get() : nullptr;
	}

	// This window's view of the shared Scene. Render-thread-only, as GetPreviewScene.
	bgl::ISceneView*
	GetPreviewView() const noexcept
	{
		return m_SceneView.Get();
	}

	// The owning reference to that view, for APIs that keep one -- gamelib's AssetManager names
	// the view each instance is placed in. Render-thread-only, as above.
	const bgl::SceneViewRef&
	GetPreviewViewRef() const noexcept
	{
		return m_SceneView;
	}

	// The renderer that owns the bgl objects. A subclass reaches the shared Scene and this window's
	// SceneView only through it -- every bgl call goes inside a Post/Invoke closure so it runs on the
	// render thread.
	Renderer*
	GetRenderer() const noexcept
	{
		return m_Desc.renderer;
	}

	// Hands the camera to the render thread, which is the only one that reads it. Returns before the
	// next frame necessarily sees it.
	void
	SetCamera(const bgl::Camera& cam);

Q_SIGNALS:
	/**
	 * Emitted from the render thread every c_FrameStatsInterval frames. Connect with
	 * Qt::QueuedConnection: a direct connection would touch the receiving widget off the GUI thread.
	 *
	 * @param meanMs   Mean frame time over the window.
	 * @param maxMs    Worst frame time in the window -- the number a stall shows up in.
	 * @param missed   Frames in the window that overran a vblank.
	 */
	void
	FrameStatsUpdated(double meanMs, double maxMs, int missed);

private:
	// Records and presents one frame. Called by the Renderer's frame loop, on the render thread.
	void
	DrawFrame();

	// Resizes the render target to (width, height) if they are valid and changed.
	void
	SyncSize(int width, int height);

	// Joins or leaves the frame loop so that membership matches SetRenderingEnabled and visibility.
	// Idempotent, so every path that can change either may simply call it.
	void
	UpdateViewport();

	void
	ReportFrameTiming(qint64 startNs);

	// Single-shot, restarted by every resizeEvent, so it only fires once the window has been still
	// long enough to call the drag finished. That firing is the only thing that resizes the
	// backbuffers.
	QTimer* m_ResizeTimer = nullptr;

	RenderTargetWindowDesc m_Desc;
	bgl::RenderTargetRef   m_RenderTarget;
	bgl::SceneViewRef      m_SceneView;

	// Non-zero only while this window is in the frame loop.
	Renderer::ViewportId m_ViewportId = 0;

	// Defaults to true so a window with no dock around it renders on visibility alone.
	bool m_RenderingEnabled = true;

	// The size the window last reported, compared against by SyncSize. GUI thread.
	uint32_t m_Width  = 1;
	uint32_t m_Height = 1;

	// Clamped copy of the desc's, so SyncSize reads one value however it was set. GUI thread.
	float m_RenderScale = 1.0f;

	// Read by DrawFrame, so written only from the render thread: the GUI thread hands new values over
	// through the Renderer rather than assigning them here, and no frame sees a half-written camera.
	bgl::Camera m_RenderCamera;
	float       m_RenderTime = 0.0f;

	// The *output* size the job's viewport carries. The target derives the grid its geometry passes
	// draw on from this and its own render scale, which is why nothing here tracks that grid.
	uint32_t m_DrawWidth  = 1;
	uint32_t m_DrawHeight = 1;

	QElapsedTimer m_FrameClock;  // monotonic clock for the timings above
	qint64        m_LastFrameStartNs = -1;

	// ~2 seconds of frames at 60Hz: long enough that one stall does not dominate the mean, short
	// enough that the readout still tracks what the viewport is doing now.
	static constexpr std::size_t c_FrameStatsWindow = 120;

	// Frames between FrameStatsUpdated emissions. Emitting per frame would queue 60 cross-thread
	// events a second to move a number no one can read that fast.
	static constexpr uint64_t c_FrameStatsInterval = 30;

	// Render thread only, like the timings above.
	core::RollingWindow<c_FrameStatsWindow> m_FrameTimes;
	uint32_t                                m_MissedFrames    = 0;
	uint64_t                                m_FramesSinceEmit = 0;
};
