#pragma once

#include <QWidget>

class QTimer;

#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

struct RenderTargetWindowDesc
{
	bgl::GraphicsHandle gfx          = nullptr;
	bgl::SceneHandle    scene        = nullptr;
	uint32_t            maxInstances = 0;
};

class RenderTargetWindow : public QWidget
{
	Q_OBJECT

public:
	explicit RenderTargetWindow(QWidget* parent = nullptr, RenderTargetWindowDesc desc = {});

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
	// Keeps the render target's backbuffers matched to the window's client size.
	void
	resizeEvent(QResizeEvent* event) override;

	// The parent layout only assigns the real size by the time the widget is shown, so
	// sync here too — resizeEvent alone can leave the target at its constructed default.
	void
	showEvent(QShowEvent* event) override;

	// This widget hosts an external DX12 swapchain and presents itself every frame, so
	// Qt must not paint the surface; returning nullptr disables Qt's own painting.
	QPaintEngine*
	paintEngine() const override
	{
		return nullptr;
	}

private:
	// Resizes the render target to (width, height) if they are valid and changed.
	void
	SyncSize(int width, int height);

	QTimer*                 m_FrameTimer = nullptr;
	RenderTargetWindowDesc  m_Desc;
	bgl::RenderTargetHandle m_RenderTarget;
	bgl::SceneViewHandle    m_SceneView;
	bgl::Camera             camera;
	uint32_t                m_Width  = 1;
	uint32_t                m_Height = 1;
};
