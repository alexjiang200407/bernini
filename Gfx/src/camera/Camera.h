#pragma once
#include "GfxBase.h"
#include "math/ShaderMatrix.h"

struct GfxCameraDesc;
struct GfxCameraTransformOptions;
struct GfxCameraProjectionOptions;

namespace gfx
{
	class IGraphics;

	class Camera : public GfxBase
	{
	public:
		explicit Camera(const GfxCameraDesc& desc);

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

		bool
		ShouldUpdate() const noexcept
		{
			return m_shouldUpdate;
		}

		void
		OnUpdated() noexcept
		{
			m_shouldUpdate = false;
		}

		ShaderMatrix
		GetViewMatrix() const noexcept
		{
			return viewMatrix;
		}

		ShaderMatrix
		GetProjMatrix() const noexcept
		{
			return projMatrix;
		}

	private:
		void
		UpdateTransform(const GfxCameraTransformOptions& transform);

		void
		UpdateProjection(const GfxCameraProjectionOptions& projection) noexcept;

	private:
		ShaderMatrix viewMatrix;
		ShaderMatrix projMatrix;
		glm::vec3    m_forwardUnit;
		glm::vec3    m_position;
		bool         m_shouldUpdate = true;
		float        m_pitch        = 0.0f;
		float        m_yaw          = 0.0f;
	};
}
