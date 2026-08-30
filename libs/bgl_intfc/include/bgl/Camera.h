#pragma once
#include <bgl/glm.h>

namespace bgl
{
	class Camera
	{
	public:
		Camera() = default;

		Camera&
		SetView(const glm::mat4& view)
		{
			m_View = view;
			return *this;
		}

		Camera&
		LookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up)
		{
			m_View = glm::lookAt(eye, target, up);
			return *this;
		}

		Camera&
		SetProjection(const glm::mat4& projection)
		{
			m_Projection = projection;
			return *this;
		}

		Camera&
		Perspective(float fovYRadians, float aspect, float nearZ, float farZ)
		{
			m_Projection = glm::perspective(fovYRadians, aspect, nearZ, farZ);
			return *this;
		}

		Camera&
		Orthographic(float left, float right, float bottom, float top, float nearZ, float farZ)
		{
			m_Projection = glm::ortho(left, right, bottom, top, nearZ, farZ);
			return *this;
		}

		Camera&
		MoveAlongView(float distance)
		{
			const glm::mat4 world   = glm::inverse(m_View);
			const glm::vec3 eye     = glm::vec3(world[3]);
			const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));  // view looks down -Z
			const glm::vec3 up      = glm::normalize(glm::vec3(world[1]));
			const glm::vec3 newEye  = eye + forward * distance;
			m_View                  = glm::lookAt(newEye, newEye + forward, up);
			return *this;
		}

		Camera&
		MoveAlongRight(float distance)
		{
			const glm::mat4 world   = glm::inverse(m_View);
			const glm::vec3 eye     = glm::vec3(world[3]);
			const glm::vec3 right   = glm::normalize(glm::vec3(world[0]));
			const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
			const glm::vec3 up      = glm::normalize(glm::vec3(world[1]));
			const glm::vec3 newEye  = eye + right * distance;
			m_View                  = glm::lookAt(newEye, newEye + forward, up);
			return *this;
		}

		Camera&
		RotateYawPitch(float yawRadians, float pitchRadians)
		{
			const glm::mat4 world   = glm::inverse(m_View);
			const glm::vec3 eye     = glm::vec3(world[3]);
			glm::vec3       forward = -glm::normalize(glm::vec3(world[2]));

			const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

			forward = glm::vec3(
				glm::rotate(glm::mat4(1.0f), yawRadians, worldUp) * glm::vec4(forward, 0.0f));

			const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
			forward               = glm::vec3(
				glm::rotate(glm::mat4(1.0f), pitchRadians, right) * glm::vec4(forward, 0.0f));

			m_View = glm::lookAt(eye, eye + forward, worldUp);
			return *this;
		}

		[[nodiscard]] const glm::mat4&
		GetView() const
		{
			return m_View;
		}

		[[nodiscard]] const glm::mat4&
		GetProjection() const
		{
			return m_Projection;
		}

		[[nodiscard]] glm::mat4
		GetViewProjection() const
		{
			return m_Projection * m_View;
		}

	private:
		glm::mat4 m_View       = glm::mat4(1.0f);
		glm::mat4 m_Projection = glm::mat4(1.0f);
	};
}
