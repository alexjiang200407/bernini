#pragma once

#include <QWidget>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <cstdint>
#include <functional>
#include <gamelib/AssetManager.h>

namespace editor
{
	struct RenderContext
	{
		bgl::IGraphics&     graphics;
		bgl::IScene&        scene;
		game::AssetManager& assets;
	};

	using RenderWork         = std::function<void(RenderContext&)>;
	using ViewportRenderWork = std::function<void(RenderContext&, const bgl::SceneViewRef&)>;

	struct ViewportDesc
	{
		uint32_t initialInstances       = 16;
		bool     taaEnabled             = true;
		float    renderScale            = 1.0f;
		float    taaReconstructionWidth = 0.4f;
	};

	/** GUI-thread widget; destruction drains its render work before releasing its view. */
	class IEditorViewport : public QWidget
	{
	public:
		using QWidget::QWidget;

		/** Synchronous on the render thread; do not retain context or wait on the GUI thread. */
		virtual void
		Invoke(const ViewportRenderWork& work) = 0;

		virtual void
		SetCamera(const bgl::Camera& camera) = 0;

		virtual void
		SetTime(float seconds) = 0;

		virtual void
		SetRenderingEnabled(bool enabled) = 0;
	};
}
