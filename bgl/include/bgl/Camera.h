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
