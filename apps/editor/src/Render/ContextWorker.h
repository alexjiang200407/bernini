#pragma once

#include "Render/invoke.h"

#include <QObject>

#include <bgl/IGraphics.h>
#include <bgl/IRenderContext.h>
#include <bgl/IScene.h>

class QThread;
class Renderer;

/**
 * A second submission context on its own thread, with a Scene of its own: for GPU work that must
 * neither stall nor be stalled by the viewports' frame loop. The same rules as Renderer: exactly
 * this thread touches the bgl objects held here, work arrives as closures, and a closure must
 * never block waiting on the GUI thread.
 *
 * The scene is this context's alone -- contexts must not share one (see IRenderContext) -- so
 * assets rendered here are uploaded here, separately from the renderer's scene.
 */
class ContextWorker : public QObject
{
	Q_OBJECT

public:
	/**
	 * Starts the worker thread and creates the context and scene. Both are created on `renderer`'s
	 * thread: pipeline construction shares the device's shader cache and slang session with the
	 * primary context, so it is serialized against the render thread rather than raced with it.
	 * Everything after -- frames, uploads, teardown -- runs on this worker's thread alone.
	 *
	 * @throws whatever context or scene creation throws, with the worker thread stopped again.
	 */
	ContextWorker(Renderer& renderer, const bgl::SceneDesc& sceneDesc);

	~ContextWorker() override;

	ContextWorker(const ContextWorker&) = delete;
	ContextWorker(ContextWorker&&)      = delete;

	ContextWorker&
	operator=(const ContextWorker&) = delete;

	ContextWorker&
	operator=(ContextWorker&&) = delete;

	// Runs `fn` on the worker thread and returns its result, blocking until it has run. Runs
	// inline when called from the worker thread.
	template <typename Fn>
	std::invoke_result_t<Fn>
	Invoke(Fn&& fn)
	{
		return render::Invoke(*this, std::forward<Fn>(fn));
	}

	// The owned context. Only valid to touch on the worker thread (inside an Invoke closure).
	[[nodiscard]] const bgl::RenderContextRef&
	GetContext() const noexcept
	{
		return m_Context;
	}

	// The owned scene. Only valid to touch on the worker thread (inside an Invoke closure).
	[[nodiscard]] const bgl::SceneRef&
	GetScene() const noexcept
	{
		return m_Scene;
	}

	// The shared graphics facade, for factories (CreateSceneView). Only valid to touch on the
	// worker thread.
	[[nodiscard]] const bgl::GraphicsRef&
	GetGraphics() const noexcept
	{
		return m_Graphics;
	}

private:
	void
	StopThread();

	QThread* m_Thread = nullptr;

	bgl::GraphicsRef      m_Graphics;
	bgl::RenderContextRef m_Context;
	bgl::SceneRef         m_Scene;
};
