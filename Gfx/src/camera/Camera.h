#pragma once
#include "GfxBase.h"

struct GfxCameraOptions;

namespace gfx
{
	class IGraphics;

	struct CameraData
	{
		glm::mat4 viewMatrix;
		glm::mat4 projMatrix;
	};

	class Camera : public GfxBase
	{
	public:
		explicit Camera(nvrhi::DeviceHandle graphics, const GfxCameraOptions& options);

		nvrhi::BufferHandle
		Update(nvrhi::CommandListHandle cmdList);

		[[nodiscard]]
		const nvrhi::BindingLayoutHandle&
		GetBindingLayout() const noexcept
		{
			return bindingLayout;
		}

		[[nodiscard]]
		nvrhi::BindingSetHandle
		GetBindingSet(nvrhi::DeviceHandle device) const noexcept;

	private:
		CameraData                 data;
		nvrhi::BufferHandle        cbuf;
		nvrhi::BindingLayoutHandle bindingLayout;
	};
}
