#include "camera/Camera.h"
#include "BindingSlots.h"
#include "GfxBase.h"
#include "ffi/util.h"
#include "graphics/Graphics.h"
#include <gfx/camera.h>

GfxResult
createCamera(Gfx gfx, GfxCameraOptions options, GfxCamera* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		gfx::ffi::validatePtr(out, "out");

		auto& gfx_   = gfx::ffi::gfxObjCast<gfx::IGraphics>(gfx);
		out->data    = new gfx::Camera{ gfx_.GetDevice(), options };
		out->destroy = gfx::ffi::deleteThunk;

		return GFX_RESULT_OK;
	});
}

namespace gfx
{
	Camera::Camera(nvrhi::DeviceHandle device, const GfxCameraOptions& options)
	{
		const float fovYRadians = glm::radians(options.fovYDegrees);
		data.projMatrix         = gfx::math::toShaderLayout(
            glm::perspective(fovYRadians, options.aspectRatio, options.nearZ, options.farZ));

		glm::vec3 camPos{ options.position[0], options.position[1], options.position[2] };
		glm::vec3 camTarget{ 0.0f, 0.0f, 0.0f };
		glm::vec3 camUp{ 0.0f, 1.0f, 0.0f };

		data.viewMatrix = gfx::math::toShaderLayout(glm::lookAt(camPos, camTarget, camUp));

		cbuf = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(CameraData),
			"CameraConstantBuffer",
			16));

		auto cmdList = device->createCommandList();
		Update(cmdList);
		cmdList->close();

		bindingLayout = device->createBindingLayout(
			nvrhi::BindingLayoutDesc{}
				.setVisibility(nvrhi::ShaderType::All)
				.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(BindingSlots::CameraCB)));
	}

	nvrhi::BufferHandle
	Camera::Update(nvrhi::CommandListHandle cmdList)
	{
		cmdList->writeBuffer(cbuf, &data, sizeof(data));
		return cbuf;
	}

	nvrhi::BindingSetHandle
	Camera::GetBindingSet(nvrhi::DeviceHandle device) const noexcept
	{
		return device->createBindingSet(
			nvrhi::BindingSetDesc{}.addItem(
				nvrhi::BindingSetItem::ConstantBuffer(BindingSlots::CameraCB, cbuf)),
			bindingLayout);
	}

}
