#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QTimer;

#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

#include "Render/Renderer.h"

struct RenderTargetWindowDesc
{
	Renderer* renderer     = nullptr;
	uint32_t  maxInstances = 0;
};

class RenderTargetWindow : public QWidget
{
	Q_OBJECT

public:
	explicit RenderTargetWindow(QWidget* parent = nullptr, RenderTargetWindowDesc desc = {});
	~RenderTargetWindow() override;

protected:
	void
	resizeEvent(QResizeEvent* event) override;

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
	PreviewScene() const noexcept
	{
		return m_Desc.renderer != nullptr ? m_Desc.renderer->GetScene().Get() : nullptr;
	}

	// This window's view of the shared Scene. Render-thread-only, as PreviewScene.
	bgl::ISceneView*
	PreviewView() const noexcept
	{
		return m_SceneView.Get();
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

private:
	// Records and presents one frame. Called by the Renderer's frame loop, on the render thread.
	void
	DrawFrame();

	// Resizes the render target to (width, height) if they are valid and changed.
	void
	SyncSize(int width, int height);

	void
	ReportFrameTiming(qint64 startNs, qint64 endNs);

	// Single-shot, restarted by every resizeEvent, so it only fires once the window has been still
	// long enough to call the drag finished. That firing is the only thing that resizes the
	// backbuffers.
	QTimer* m_ResizeTimer = nullptr;

	RenderTargetWindowDesc m_Desc;
	bgl::RenderTargetRef   m_RenderTarget;
	bgl::SceneViewRef      m_SceneView;

	// Non-zero only while the window is showing, which is the only time it is in the frame loop.
	Renderer::ViewportId m_ViewportId = 0;

	// The size the window last reported, compared against by SyncSize. GUI thread.
	uint32_t m_Width  = 1;
	uint32_t m_Height = 1;

	// Read by DrawFrame, so written only from the render thread: the GUI thread hands new values over
	// through the Renderer rather than assigning them here, and no frame sees a half-written camera.
	bgl::Camera m_RenderCamera;
	uint32_t    m_RenderWidth  = 1;
	uint32_t    m_RenderHeight = 1;

	QElapsedTimer m_FrameClock;  // monotonic clock for the timings above
	qint64        m_LastFrameStartNs = -1;
	qint64        m_LastFrameEndNs   = -1;
};
