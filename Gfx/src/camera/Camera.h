#pragma once
#include "GfxBase.h"
#include "math/ShaderMatrix.h"

struct GfxCameraDesc;
struct GfxCameraTransformOptions;
struct GfxCameraProjectionOptions;

namespace gfx
{
	class IGraphics;

	struct CameraCBuf
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
			return m_bindingLayout;
		}

		[[nodiscard]]
		nvrhi::BindingSetHandle
		GetBindingSet(nvrhi::DeviceHandle device) const noexcept;

		void
		SetShouldUpdated() noexcept
		{
			m_shouldUpdate = true;
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
		CameraCBuf                 m_cbufData;
		glm::vec3                  m_forwardUnit;
		glm::vec3                  m_position;
		nvrhi::BufferHandle        m_cbuf;
		nvrhi::BindingLayoutHandle m_bindingLayout;
		bool                       m_shouldUpdate = true;
		float                      m_pitch        = 0.0f;
		float                      m_yaw          = 0.0f;
	};
}
