#include "OrbitCamera.h"
#include <algorithm>
#include <bgl/Camera.h>
#include <cmath>

namespace editor
{
	namespace
	{
		constexpr float c_RadiansPerPixel = 0.01f;
		constexpr float c_PitchLimit      = 1.55f;  // just under 90 degrees
	}

	void
	OrbitCamera::FocusOn(
		const glm::vec3& center,
		const float      radius,
		const float      yaw,
		const float      pitch) noexcept
	{
		m_FocusCenter = center;
		m_FocusRadius = radius;

		// Pull back far enough that the focus sphere fits the field of view, with some margin.
		m_Distance = radius * 3.0f;
		m_Yaw      = yaw;
		m_Pitch    = std::clamp(pitch, -c_PitchLimit, c_PitchLimit);
	}

	void
	OrbitCamera::Orbit(const float dxPixels, const float dyPixels) noexcept
	{
		m_Yaw -= dxPixels * c_RadiansPerPixel;
		m_Pitch = std::clamp(m_Pitch + dyPixels * c_RadiansPerPixel, -c_PitchLimit, c_PitchLimit);
	}

	void
	OrbitCamera::Pan(const float dxPixels, const float dyPixels) noexcept
	{
		const glm::vec3 forward = glm::normalize(m_FocusCenter - GetEyePosition());
		const glm::vec3 right   = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
		const glm::vec3 up      = glm::cross(right, forward);

		const float scale = m_Distance * 0.002f;
		m_FocusCenter += right * (-dxPixels * scale);
		m_FocusCenter += up * (dyPixels * scale);
	}

	void
	OrbitCamera::Dolly(const float wheelSteps) noexcept
	{
		// Geometric, so each notch feels the same at any distance.
		m_Distance = std::clamp(
			m_Distance * std::pow(0.9f, wheelSteps),
			m_FocusRadius * 0.1f,
			m_FocusRadius * 50.0f);
	}

	glm::vec3
	OrbitCamera::GetEyePosition() const noexcept
	{
		const glm::vec3 direction(
			std::cos(m_Pitch) * std::sin(m_Yaw),
			std::sin(m_Pitch),
			std::cos(m_Pitch) * std::cos(m_Yaw));
		return m_FocusCenter + direction * m_Distance;
	}

	bgl::Camera
	OrbitCamera::GetCamera(const float aspect) const noexcept
	{
		auto cam = bgl::Camera();
		cam.LookAt(GetEyePosition(), m_FocusCenter, glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(45.0f),
				aspect,
				std::max(0.001f, m_FocusRadius * 0.01f),
				m_Distance + m_FocusRadius * 50.0f);
		return cam;
	}
}
