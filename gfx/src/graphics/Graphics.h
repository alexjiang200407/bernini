#pragma once
#include "GfxBase.h"
#include <gfx/ffi/gfx.h>

namespace gfx
{
	class Camera;
	class SceneData;
	class MeshFactory;

	class IGraphics : public GfxBase
	{
	public:
		virtual void
		DrawFrame(Camera& camera, SceneData& sceneData) = 0;

		[[nodiscard]]
		virtual nvrhi::DeviceHandle
		GetDevice() noexcept = 0;

		static IGraphics*
		Create(const GfxOptions& options = {});

	protected:
		int m_windowWidth = 0, m_windowHeight = 0;
	};

}
