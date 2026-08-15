#pragma once

#include <bgl/Camera.h>

namespace editor
{
	/**
	 * The orbit-camera arithmetic every preview viewport shares: yaw and pitch around a focus
	 * point, a dollied distance, pan across the view plane. Pure math -- the Qt mouse glue stays
	 * in the windows, so this is pinnable without one.
	 */
	class OrbitCamera
	{
	public:
		/**
		 * Re-centres on `center`, framing a sphere of `radius`: pulled back to 3x, the view reset
		 * to `yaw`/`pitch` (radians; positive pitch puts the eye above the center).
		 */
		void
		FocusOn(
			const glm::vec3& center,
			float            radius,
			float            yaw   = 0.0f,
			float            pitch = 0.0f) noexcept;

		/** Rotates by a mouse delta in pixels; pitch is clamped just short of the poles. */
		void
		Orbit(float dxPixels, float dyPixels) noexcept;

		/** Slides the focus point across the view plane, scaled so the model tracks the cursor. */
		void
		Pan(float dxPixels, float dyPixels) noexcept;

		/** Dollies geometrically per wheel notch, clamped so the focus sphere never degenerates. */
		void
		Dolly(float wheelSteps) noexcept;

		[[nodiscard]] glm::vec3
		GetEyePosition() const noexcept;

		/** The camera for this state at `aspect`, near/far derived from the focus sphere. */
		[[nodiscard]] bgl::Camera
		GetCamera(float aspect) const noexcept;

		[[nodiscard]] const glm::vec3&
		GetFocusCenter() const noexcept
		{
			return m_FocusCenter;
		}

		[[nodiscard]] float
		GetDistance() const noexcept
		{
			return m_Distance;
		}

		[[nodiscard]] float
		GetPitch() const noexcept
		{
			return m_Pitch;
		}

	private:
		glm::vec3 m_FocusCenter = glm::vec3(0.0f);
		float     m_FocusRadius = 1.0f;
		float     m_Distance    = 3.0f;
		float     m_Yaw         = 0.0f;
		float     m_Pitch       = 0.0f;
	};
}
