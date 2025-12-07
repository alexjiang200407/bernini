#pragma once
#include "GfxBase.h"
#include "math/ShaderMatrix.h"

struct GfxCameraDesc;
struct GfxCameraTransformOptions;
struct GfxCameraProjectionOptions;

namespace gfx
{
	class IGraphics;

	struct CameraData
	{
		math::ShaderMatrix viewMatrix;
		math::ShaderMatrix projMatrix;
	};

	class Camera : public GfxBase
	{
	public:
		explicit Camera(nvrhi::DeviceHandle graphics, const GfxCameraDesc& desc);

		nvrhi::BufferHandle
		UpdateBuffer(nvrhi::CommandListHandle cmdList);

		[[nodiscard]]
		const nvrhi::BindingLayoutHandle&
		GetBindingLayout() const noexcept
		{
			return bindingLayout;
		}

		[[nodiscard]]
		nvrhi::BindingSetHandle
		GetBindingSet(nvrhi::DeviceHandle device) const noexcept;

		void
		SetShouldUpdated() noexcept
		{
			shouldUpdate = true;
		}

		void
		MoveAlongView(float delta) noexcept;

		void
		MoveAlongRight(float delta) noexcept;

		void
		RotateYawPitch(float yaw, float pitch) noexcept;

	private:
		void
		UpdateTransform(const GfxCameraTransformOptions& transform);

		void
		UpdateProjection(const GfxCameraProjectionOptions& projection) noexcept;

	private:
		CameraData                 data;
		glm::vec3                  forwardUnit;
		glm::vec3                  position;
		nvrhi::BufferHandle        cbuf;
		nvrhi::BindingLayoutHandle bindingLayout;
		bool                       shouldUpdate = true;
		float                      pitch        = 0.0f;
		float                      yaw          = 0.0f;
	};
}
