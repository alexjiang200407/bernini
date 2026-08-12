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

		// The animation clock, in seconds; VAT playback derives its pose from it. A caller that
		// never sets it draws every instance at its spawn phase.
		float time = 0.0f;
	};
}
