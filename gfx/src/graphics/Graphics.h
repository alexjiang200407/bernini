#pragma once
#include "GfxBase.h"
#include <gfx/ffi/gfx.h>

namespace gfx
{
	class Camera;
	class MeshRegistry;
	class MeshFactory;

	class IGraphics : public GfxBase
	{
	public:
		virtual void
		DrawFrame(Camera& camera) = 0;

		[[nodiscard]]
		virtual nvrhi::DeviceHandle
		GetDevice() noexcept = 0;

		static IGraphics*
		Create(const GfxOptions& options = {});

		virtual MeshFactory&
		GetMeshFactory() noexcept = 0;

		virtual MeshRegistry&
		GetMeshRegistry() = 0;

	protected:
		int m_windowWidth = 0, m_windowHeight = 0;
	};

}
