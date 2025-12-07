#include "camera/Camera.h"
#include "BindingSlots.h"
#include "GfxBase.h"
#include "ffi/util.h"
#include "graphics/Graphics.h"
#include "math/util.h"
#include <gfx/ffi/camera.h>

GfxResult
createCamera(Gfx gfx, GfxCameraDesc desc, GfxCamera* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		gfx::ffi::validatePtr(out, "out");

		auto& gfx_   = gfx::ffi::gfxObjCast<gfx::IGraphics>(gfx);
		out->data    = new gfx::Camera{ gfx_.GetDevice(), desc };
		out->destroy = gfx::ffi::deleteThunk;

		return GFX_RESULT_OK;
	});
}

GfxResult
cameraMoveAlongView(GfxCamera camera, float delta)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& camera_ = gfx::ffi::gfxObjCast<gfx::Camera>(camera);
		camera_.MoveAlongView(delta);
		return GFX_RESULT_OK;
	});
}

GfxResult
cameraMoveAlongRight(GfxCamera camera, float delta)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& camera_ = gfx::ffi::gfxObjCast<gfx::Camera>(camera);
		camera_.MoveAlongRight(delta);
		return GFX_RESULT_OK;
	});
}

GfxResult
cameraRotateYawPitch(GfxCamera camera, float deltaYaw, float deltaPitch)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& camera_ = gfx::ffi::gfxObjCast<gfx::Camera>(camera);
		camera_.RotateYawPitch(deltaYaw, deltaPitch);
		return GFX_RESULT_OK;
	});
}

namespace gfx
{
	Camera::Camera(nvrhi::DeviceHandle device, const GfxCameraDesc& desc)
	{
		UpdateTransform(desc.transform);
		UpdateProjection(desc.projection);

		cbuf = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(CameraData),
			"CameraConstantBuffer",
			16));

		bindingLayout = device->createBindingLayout(
			nvrhi::BindingLayoutDesc{}
				.setVisibility(nvrhi::ShaderType::All)
				.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(BindingSlots::CameraCB)));
	}

	nvrhi::BufferHandle
	Camera::UpdateBuffer(nvrhi::CommandListHandle cmdList)
	{
		if (!shouldUpdate)
		{
			return cbuf;
		}

		cmdList->writeBuffer(cbuf, &data, sizeof(data));
		shouldUpdate = false;
		return cbuf;
	}

	void
	Camera::MoveAlongView(float delta) noexcept
	{
		position += forwardUnit * delta;

		glm::vec3 target = position + forwardUnit;
		data.viewMatrix  = glm::lookAt(position, target, math::constants::UP_VEC);

		shouldUpdate = true;
	}

	void
	Camera::MoveAlongRight(float delta) noexcept
	{
		glm::vec3 right = glm::normalize(glm::cross(forwardUnit, math::constants::UP_VEC));

		position += right * delta;

		glm::vec3 target = position + forwardUnit;
		data.viewMatrix  = glm::lookAt(position, target, math::constants::UP_VEC);

		shouldUpdate = true;
	}

	void
	Camera::RotateYawPitch(float yawDelta, float pitchDelta) noexcept
	{
		const float maxPitch = glm::radians(89.0f);
		const float minPitch = glm::radians(-89.0f);
		pitch                = glm::clamp(pitchDelta, minPitch, maxPitch);
		yaw += yawDelta;

		forwardUnit.x = cos(pitch) * sin(yaw);
		forwardUnit.y = sin(pitch);
		forwardUnit.z = cos(pitch) * cos(yaw);
		forwardUnit   = glm::normalize(forwardUnit);

		glm::vec3 target = position + forwardUnit;
		data.viewMatrix  = glm::lookAt(position, target, math::constants::UP_VEC);

		shouldUpdate = true;
	}

	void
	Camera::UpdateTransform(const GfxCameraTransformOptions& tr)
	{
		glm::vec3 forward = math::toGlm(tr.forward);

		if (glm::length(forward) < math::constants::EPSILON)
		{
			throw GfxException{ GFX_RESULT_ERROR_INVALID_ARGUMENT,
				                "Invalid Argument",
				                "Camera forward vector cannot be zero." };
		}

		position = math::toGlm(tr.position);

		forwardUnit = glm::normalize(forward);
		pitch       = glm::asin(forwardUnit.y);
		yaw         = glm::atan(forwardUnit.x, forwardUnit.z);

		glm::vec3 target = position + forwardUnit;

		data.viewMatrix = glm::lookAt(position, target, math::constants::UP_VEC);
	}

	void
	Camera::UpdateProjection(const GfxCameraProjectionOptions& projection) noexcept
	{
		const float fovYRadians = glm::radians(projection.fovYDeg);
		data.projMatrix         = glm::perspective(
            fovYRadians,
            projection.aspectRatio,
            projection.nearZ,
            projection.farZ);
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
