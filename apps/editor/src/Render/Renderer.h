#pragma once

#include <QObject>
#include <QSemaphore>

#include <bgl/IGraphics.h>
#include <bgl/IScene.h>

class QThread;
class QTimer;

/**
 * Owns every bgl object the editor renders through -- the one Graphics and the one Scene -- and runs
 * the thread that is the only one ever to touch them. Work reaches the renderer as closures: Post()
 * runs one and returns, Invoke() runs one and hands back its result.
 *
 * This is what keeps a busy GUI thread from starving the viewports: the node graph's mouse-move flood
 * stays on the GUI event loop while frames present here. Because exactly one thread dereferences the
 * bgl objects, no scene state needs a lock.
 *
 * The frame loop lives here too -- a zero-interval timer on this thread draws every registered
 * viewport -- so pacing is decided by the vsync-locked Present and not by how busy the GUI is.
 *
 * A closure runs on the render thread and must not throw across that boundary: Invoke() carries an
 * exception back to its caller, but a Post() closure has no caller left to catch it, so it must
 * handle its own bgl errors. Deadlock-free by one rule: a closure must never block waiting on the GUI
 * thread; Invoke() runs inline when already on the render thread so a closure may call back in.
 */
class Renderer : public QObject
{
	Q_OBJECT

public:
	// Identifies a registered viewport. Never 0, which stands for "not registered".
	using ViewportId = uint64_t;

	Renderer(const bgl::GraphicsOptions& gfxOpts, const bgl::SceneDesc& sceneDesc);
	~Renderer() override;

	// Runs `fn` on the render thread. Returns immediately when cross-thread; runs inline when the
	// caller is already the render thread.
	void
	Post(std::function<void()> fn);

	// Runs `fn` on the render thread and returns its result, blocking until it has run. Runs inline
	// when called from the render thread.
	template <typename Fn>
	std::invoke_result_t<Fn>
	Invoke(Fn&& fn);

	[[nodiscard]] bool
	OnRenderThread() const noexcept;

	/**
	 * Adds `draw` to the frame loop, which calls it on the render thread until it is removed, and
	 * starts the loop if this is the first viewport.
	 *
	 * @return The id to hand back to RemoveViewport.
	 */
	[[nodiscard]] ViewportId
	AddViewport(std::function<void()> draw);

	/**
	 * Takes the viewport `id` names out of the frame loop, stopping the loop if it was the last one.
	 *
	 * Blocks until the render thread has done it, so once this returns neither `draw` nor anything
	 * posted before it can still be running or pending -- which is what makes it safe for a widget to
	 * destroy itself immediately afterwards.
	 */
	void
	RemoveViewport(ViewportId id);

	// The owned Graphics. Only valid to touch on the render thread (inside a Post/Invoke closure).
	[[nodiscard]] const bgl::GraphicsRef&
	GetGraphics() const noexcept
	{
		return m_Graphics;
	}

	// The owned Scene. Only valid to touch on the render thread (inside a Post/Invoke closure).
	[[nodiscard]] const bgl::SceneRef&
	GetScene() const noexcept
	{
		return m_Scene;
	}

private:
	// Draws every registered viewport, in registration order.
	void
	Frame();

	void
	StopThread();

	struct Viewport
	{
		ViewportId            id = 0;
		std::function<void()> draw;
	};

	QThread* m_Thread = nullptr;

	// Created on, and owned by, the render thread -- a timer only ever fires on the thread its object
	// lives in. Runs only while a viewport is registered, or it would spin the thread for nothing.
	QTimer* m_FrameTimer = nullptr;

	std::vector<Viewport> m_Viewports;
	ViewportId            m_NextViewportId = 1;

	bgl::GraphicsRef m_Graphics;
	bgl::SceneRef    m_Scene;
};

template <typename Fn>
std::invoke_result_t<Fn>
Renderer::Invoke(Fn&& fn)
{
	using Result = std::invoke_result_t<Fn>;

	if (OnRenderThread())
		return std::invoke(std::forward<Fn>(fn));

	// A throw on the render thread must come back to the caller, not escape into that thread's event
	// loop (where it would terminate) and leave the semaphore unreleased (deadlocking this wait).
	// Reference capture is safe: acquire() blocks here until the closure has released, so the captures
	// outlive it.
	QSemaphore         done;
	std::exception_ptr error;
	if constexpr (std::is_void_v<Result>)
	{
		QMetaObject::invokeMethod(
			this,
			[&] {
				try
				{
					std::invoke(fn);
				}
				catch (...)
				{
					error = std::current_exception();
				}
				done.release();
			},
			Qt::QueuedConnection);
		done.acquire();
		if (error)
			std::rethrow_exception(error);
	}
	else
	{
		Result result{};
		QMetaObject::invokeMethod(
			this,
			[&] {
				try
				{
					result = std::invoke(fn);
				}
				catch (...)
				{
					error = std::current_exception();
				}
				done.release();
			},
			Qt::QueuedConnection);
		done.acquire();
		if (error)
			std::rethrow_exception(error);
		return result;
	}
}
