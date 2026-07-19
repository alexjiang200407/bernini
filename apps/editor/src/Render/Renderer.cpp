#include "Render/Renderer.h"

#include <QThread>
#include <QTimer>

Renderer::Renderer(const bgl::GraphicsOptions& gfxOpts, const bgl::SceneDesc& sceneDesc)
{
	m_Thread = new QThread;
	m_Thread->setObjectName("bgl-render");

	// Must happen before the thread runs, and while this object still lives on the calling thread --
	// which is also why a Renderer takes no parent: a parented QObject cannot be moved.
	moveToThread(m_Thread);
	m_Thread->start();

	try
	{
		Invoke([&] {
			try
			{
				m_Graphics = bgl::CreateGraphics(gfxOpts);
				m_Scene    = m_Graphics->CreateScene(sceneDesc);

				// Parented to this, so it belongs to the render thread and is destroyed from it.
				m_FrameTimer = new QTimer(this);

				// Present is vsync-locked, and DXGI queues frames until Present blocks -- so the timer
				// does not pace anything, it only re-drives the loop once the previous frame returns. It
				// must stay zero-interval: any non-zero one is quantised to the Windows tick (~15.6ms),
				// which would make the timer the pacer and stretch frames to ~33ms.
				m_FrameTimer->setTimerType(Qt::PreciseTimer);
				connect(m_FrameTimer, &QTimer::timeout, this, [this] { Frame(); });
			}
			catch (...)
			{
				// Whatever got as far as being assigned is released here rather than by the constructor's
				// unwind, which runs on the calling thread with this one already stopped.
				delete m_FrameTimer;
				m_FrameTimer = nullptr;

				m_Scene    = nullptr;
				m_Graphics = nullptr;
				throw;
			}
		});
	}
	catch (...)
	{
		StopThread();
		throw;
	}
}

Renderer::~Renderer()
{
	// Everything bgl owns has to be released on the thread that owns it, and the timer with it: a
	// QObject destroyed from another thread cannot stop its own timers.
	Invoke([&] {
		delete m_FrameTimer;
		m_FrameTimer = nullptr;

		m_Viewports.clear();
		m_Scene    = nullptr;
		m_Graphics = nullptr;
	});

	StopThread();
}

void
Renderer::StopThread()
{
	m_Thread->quit();
	m_Thread->wait();

	delete m_Thread;
	m_Thread = nullptr;
}

void
Renderer::Post(std::function<void()> fn)
{
	QMetaObject::invokeMethod(this, std::move(fn), Qt::AutoConnection);
}

bool
Renderer::OnRenderThread() const noexcept
{
	return thread() == QThread::currentThread();
}

Renderer::ViewportId
Renderer::AddViewport(std::function<void()> draw)
{
	return Invoke([&] {
		const ViewportId id = m_NextViewportId++;
		m_Viewports.push_back({ id, std::move(draw) });

		if (!m_FrameTimer->isActive())
			m_FrameTimer->start(0);

		return id;
	});
}

void
Renderer::RemoveViewport(ViewportId id)
{
	Invoke([&] {
		std::erase_if(m_Viewports, [id](const Viewport& viewport) { return viewport.id == id; });

		if (m_Viewports.empty())
			m_FrameTimer->stop();
	});
}

void
Renderer::Frame()
{
	for (const Viewport& viewport : m_Viewports) viewport.draw();
}
