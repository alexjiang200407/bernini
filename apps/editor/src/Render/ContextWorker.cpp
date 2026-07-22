#include "Render/ContextWorker.h"

#include "Render/Renderer.h"

#include <QThread>

ContextWorker::ContextWorker(Renderer& renderer, const bgl::SceneDesc& sceneDesc)
{
	m_Thread = new QThread;
	m_Thread->setObjectName("bgl-context-worker");

	// Must happen before the thread runs, and while this object still lives on the calling thread
	// -- which is also why a ContextWorker takes no parent: a parented QObject cannot be moved.
	moveToThread(m_Thread);
	m_Thread->start();

	try
	{
		renderer.Invoke([&] {
			m_Graphics = renderer.GetGraphics();
			m_Context  = m_Graphics->CreateRenderContext();
			m_Scene    = m_Graphics->CreateScene(sceneDesc);
		});
	}
	catch (...)
	{
		// Release on the worker thread like the destructor would; whatever was assigned before the
		// throw is context-affine to this worker from the moment it exists.
		Invoke([&] {
			m_Scene    = nullptr;
			m_Context  = nullptr;
			m_Graphics = nullptr;
		});
		StopThread();
		throw;
	}
}

ContextWorker::~ContextWorker()
{
	// The context's teardown flushes its queue and hands its resources back, so it runs on the
	// thread that drove it, like every other touch.
	Invoke([&] {
		m_Scene    = nullptr;
		m_Context  = nullptr;
		m_Graphics = nullptr;
	});

	StopThread();
}

void
ContextWorker::StopThread()
{
	m_Thread->quit();
	m_Thread->wait();

	delete m_Thread;
	m_Thread = nullptr;
}
