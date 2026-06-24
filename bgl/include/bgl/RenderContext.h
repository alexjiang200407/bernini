#pragma once
#include <bgl/Camera.h>
#include <bgl/IScene.h>
#include <bgl/Viewport.h>

namespace bgl
{
	struct RenderContext
	{
		IScene*  scene = nullptr;
		Camera   camera;
		Viewport viewport;
	};
}
