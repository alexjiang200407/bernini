#pragma once

#include <QWidget>
#include <bgl/Camera.h>
#include <bgl/ISceneView.h>
#include <core/glm.h>
#include <cstdint>
#include <functional>

namespace bgl
{
	class IGraphics;
	class IScene;
}

namespace game
{
	class AssetManager;
}

namespace editor
{
	struct RenderContext
	{
		bgl::IGraphics&     graphics;
		bgl::IScene&        scene;
		game::AssetManager& assets;
	};

	struct ViewportDesc
	{
		uint32_t initialInstances = 16;
		bool     taaEnabled       = true;
	};

	/** GUI-thread widget; destruction drains its render work before releasing its view. */
	class IEditorViewport : public QWidget
	{
	public:
		using QWidget::QWidget;

		/** Synchronous on the render thread; do not retain context or wait on the GUI thread. */
		virtual void
		Invoke(const std::function<void(RenderContext&, const bgl::SceneViewRef&)>& work) = 0;

		virtual void
		SetCamera(const bgl::Camera& camera) = 0;

		virtual void
		SetTime(float seconds) = 0;

		virtual void
		SetRenderingEnabled(bool enabled) = 0;
	};
}
