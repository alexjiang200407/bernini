#pragma once
#include <bgl/Camera.h>
#include <bgl/ISceneView.h>
#include <bgl/Viewport.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	struct RenderJob
	{
		core::SharedRef<ISceneView> view = nullptr;
		Camera                      camera;
		Viewport                    viewport;

		// The animation clock, in seconds.
		float time = 0.0f;
	};
}
