#include "Render/Renderer.h"

#include <QEventLoop>
#include <QThread>
#include <QTimer>

Renderer::Renderer(
	const bgl::GraphicsOptions& gfxOpts,
	const bgl::SceneDesc&       sceneDesc,
	RendererWait                wait)
{
	m_Thread = new QThread;
	m_Thread->setObjectName("bgl-render");

	// Must happen before the thread runs, and while this object still lives on the calling thread --
	// which is also why a Renderer takes no parent: a parented QObject cannot be moved.
	moveToThread(m_Thread);
	m_Thread->start();

	// Carried back rather than thrown: a posted closure has no caller to throw to, and the two
	// waits below must fail the same way.
	auto failure = std::exception_ptr();
	auto built   = std::atomic<bool>(false);

	const auto build = [&]() {
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

			failure = std::current_exception();
		}

		// Last, and released: the poll below reads it, and must see everything above it.
		built.store(true, std::memory_order_release);
	};

	if (wait == RendererWait::kPumpEventLoop)
	{
		Post(build);

		// Spins rather than blocks, so a startup screen already on this thread keeps painting while
		// the render thread compiles. Polled rather than woken, for the reason
		// background::RunWithLoadingScreen polls: a nested loop need not service the posted-event
		// source that a queued quit() would arrive on, and timers are serviced either way.
		QEventLoop loop;
		QTimer     poll;
		poll.setInterval(30);
		QObject::connect(&poll, &QTimer::timeout, &loop, [&loop, &built]() {
			if (built.load(std::memory_order_acquire))
				loop.quit();
		});
		poll.start();
		loop.exec();

		// exec() can return with the build still running: QCoreApplication::exit() -- a session
		// logoff -- exits every event loop on this thread, this one among them. `build` holds
		// references to this frame, so it is waited out rather than left to write into a dead one.
		while (!built.load(std::memory_order_acquire) && m_Thread != nullptr &&
		       !m_Thread->isFinished())
			QThread::msleep(5);
	}
	else
	{
		Invoke(build);
	}

	// Neither wait guarantees the closure ran: Invoke gives up on a render thread that is already
	// gone, and the loop above stops for one that dies mid-build. Left alone, that is a Renderer
	// whose GetGraphics() is null and whose first viewport dereferences it.
	if (!built.load(std::memory_order_acquire))
	{
		StopThread();
		throw std::runtime_error(
			"Renderer: the render thread stopped before it built the graphics");
	}

	if (failure)
	{
		StopThread();
		std::rethrow_exception(failure);
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

bool
Renderer::AwaitClosure(QSemaphore& done) const
{
	for (;;)
	{
		if (done.tryAcquire())
			return true;

		// A dead render thread can never release the semaphore, and a plain acquire() would turn
		// the caller -- window close among them -- into a permanent hang.
		if (m_Thread == nullptr || m_Thread->isFinished())
		{
			qCritical("Renderer: the render thread is gone; a queued closure will never run");
			return false;
		}

		if (done.tryAcquire(1, 1000))
			return true;
	}
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
	for (auto it = m_Viewports.begin(); it != m_Viewports.end();)
	{
		// An exception escaping into this thread's event loop ends the thread, after which every
		// Invoke -- the window close among them -- blocks forever. Drop the viewport instead: it
		// stops drawing, and the rest of the editor keeps working.
		try
		{
			it->draw();
			++it;
		}
		catch (const std::exception& e)
		{
			qCritical(
				"Renderer: dropping viewport %llu, its draw threw: %s",
				static_cast<unsigned long long>(it->id),
				e.what());
			it = m_Viewports.erase(it);
		}
	}

	if (m_Viewports.empty())
		m_FrameTimer->stop();
}
