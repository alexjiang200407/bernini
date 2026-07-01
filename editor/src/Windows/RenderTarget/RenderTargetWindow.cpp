#include "Windows/RenderTarget/RenderTargetWindow.h"
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <bgl/IGraphics.h>

RenderTargetWindow::RenderTargetWindow(QWidget* parent, RenderTargetWindowDesc desc) :
	QWidget(parent), m_Desc(std::move(desc))
{
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
	connect(m_FrameTimer, &QTimer::timeout, this, [this]() {
		SyncSize(width(), height());
		DrawFrame(m_Desc.gfx.Get());
	});
	m_FrameTimer->start(16);
}

void
RenderTargetWindow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	SyncSize(event->size().width(), event->size().height());
}

void
RenderTargetWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	SyncSize(width(), height());
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
	m_Desc.gfx->Resize(m_RenderTarget, width, height);
}
