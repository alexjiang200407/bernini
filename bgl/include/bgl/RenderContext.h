#pragma once
#include <bgl/Camera.h>
#include <bgl/IScene.h>
#include <bgl/Viewport.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	struct RenderContext
	{
		core::SharedRef<IScene> scene = nullptr;
		Camera                  camera;
		Viewport                viewport;
	};
}
