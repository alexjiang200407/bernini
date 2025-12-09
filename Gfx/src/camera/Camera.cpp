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

		m_cbuf = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(CameraCBuf),
			"CameraConstantBuffer",
			16));

		m_bindingLayout = device->createBindingLayout(
			nvrhi::BindingLayoutDesc{}
				.setVisibility(nvrhi::ShaderType::All)
				.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(BindingSlots::CameraCB)));
	}

	nvrhi::BufferHandle
	Camera::UpdateBuffer(nvrhi::CommandListHandle cmdList)
	{
		if (!m_shouldUpdate)
		{
			return m_cbuf;
		}

		cmdList->writeBuffer(m_cbuf, &m_cbufData, sizeof(m_cbufData));
		m_shouldUpdate = false;
		return m_cbuf;
	}

	void
	Camera::MoveAlongView(float delta) noexcept
	{
		m_position += m_forwardUnit * delta;

		glm::vec3 target      = m_position + m_forwardUnit;
		m_cbufData.viewMatrix = glm::lookAt(m_position, target, math::constants::UP_VEC);

		m_shouldUpdate = true;
	}

	void
	Camera::MoveAlongRight(float delta) noexcept
	{
		glm::vec3 right = glm::normalize(glm::cross(m_forwardUnit, math::constants::UP_VEC));

		m_position += right * delta;

		glm::vec3 target      = m_position + m_forwardUnit;
		m_cbufData.viewMatrix = glm::lookAt(m_position, target, math::constants::UP_VEC);

		m_shouldUpdate = true;
	}

	void
	Camera::RotateYawPitch(float yawDelta, float pitchDelta) noexcept
	{
		static const float maxPitch = glm::radians(89.0f);
		static const float minPitch = glm::radians(-89.0f);

		m_pitch = glm::clamp(m_pitch + pitchDelta, minPitch, maxPitch);
		m_yaw   = fmod(m_yaw + yawDelta, glm::two_pi<float>());

		m_forwardUnit.x = cos(m_pitch) * sin(m_yaw);
		m_forwardUnit.y = sin(m_pitch);
		m_forwardUnit.z = cos(m_pitch) * cos(m_yaw);
		m_forwardUnit   = glm::normalize(m_forwardUnit);

		glm::vec3 target      = m_position + m_forwardUnit;
		m_cbufData.viewMatrix = glm::lookAt(m_position, target, math::constants::UP_VEC);

		m_shouldUpdate = true;
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

		m_position = math::toGlm(tr.position);

		m_forwardUnit = glm::normalize(forward);
		m_pitch       = glm::asin(m_forwardUnit.y);
		m_yaw         = glm::atan(m_forwardUnit.x, m_forwardUnit.z);

		glm::vec3 target = m_position + m_forwardUnit;

		m_cbufData.viewMatrix = glm::lookAt(m_position, target, math::constants::UP_VEC);
	}

	void
	Camera::UpdateProjection(const GfxCameraProjectionOptions& projection) noexcept
	{
		const float fovYRadians = glm::radians(projection.fovYDeg);
		m_cbufData.projMatrix   = glm::perspective(
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
				nvrhi::BindingSetItem::ConstantBuffer(BindingSlots::CameraCB, m_cbuf)),
			m_bindingLayout);
	}

}
