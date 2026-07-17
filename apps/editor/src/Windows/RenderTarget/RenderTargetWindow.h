#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QTimer;

#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

struct RenderTargetWindowDesc
{
	bgl::GraphicsRef gfx          = nullptr;
	bgl::SceneRef    scene        = nullptr;
	uint32_t         maxInstances = 0;
};

class RenderTargetWindow : public QWidget
{
	Q_OBJECT

public:
	explicit RenderTargetWindow(QWidget* parent = nullptr, RenderTargetWindowDesc desc = {});

	// The view this window draws. Held, so it can outlive the window in something that places
	// instances into it -- the shared AssetManager does exactly that.
	bgl::SceneViewRef
	View() const noexcept
	{
		return m_SceneView;
	}

	void
	DrawFrame(bgl::IGraphics* gfx)
	{
		auto rc     = bgl::RenderContext();
		rc.camera   = camera;
		rc.view     = m_SceneView;
		rc.viewport = bgl::Viewport(m_Width, m_Height);

		gfx->DrawFrame(m_RenderTarget, rc);
	}

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

	bgl::IScene*
	PreviewScene() const noexcept
	{
		return m_Desc.scene.Get();
	}

	bgl::ISceneView*
	PreviewView() const noexcept
	{
		return m_SceneView.Get();
	}

	bgl::IGraphics*
	PreviewGraphics() const noexcept
	{
		return m_Desc.gfx.Get();
	}

	void
	SetCamera(const bgl::Camera& cam) noexcept
	{
		camera = cam;
	}

private:
	// Resizes the render target to (width, height) if they are valid and changed.
	void
	SyncSize(int width, int height);

	void
	ReportFrameTiming(qint64 startNs, qint64 endNs);

	QTimer* m_FrameTimer = nullptr;

	// Single-shot, restarted by every resizeEvent, so it only fires once the window has been still
	// long enough to call the drag finished. That firing is the only thing that resizes the
	// backbuffers.
	QTimer* m_ResizeTimer = nullptr;

	RenderTargetWindowDesc m_Desc;
	bgl::RenderTargetRef   m_RenderTarget;
	bgl::SceneViewRef      m_SceneView;
	bgl::Camera            camera;
	uint32_t               m_Width  = 1;
	uint32_t               m_Height = 1;

	QElapsedTimer m_FrameClock;  // monotonic clock for the timings above
	qint64        m_LastFrameStartNs = -1;
	qint64        m_LastFrameEndNs   = -1;
};
