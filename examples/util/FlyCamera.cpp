#include "FlyCamera.h"

#include <SDL3/SDL.h>

namespace demo
{
	namespace
	{
		constexpr float c_MouseRadiansPerPixel = 0.005f;
	}

	float
	DeltaClock::Tick() noexcept
	{
		const auto  now = std::chrono::steady_clock::now();
		const float dt  = std::chrono::duration<float>(now - m_last).count();
		m_last          = now;
		return dt;
	}

	bool
	ApplyFlyCam(bgl::Camera& camera, float dt, float moveUnitsPerSecond) noexcept
	{
		const bool* keys = SDL_GetKeyboardState(nullptr);

		const float moveSpeed = dt * moveUnitsPerSecond;
		float       forward   = 0.0f;
		float       right     = 0.0f;
		if (keys[SDL_SCANCODE_W])
			forward += moveSpeed;
		if (keys[SDL_SCANCODE_S])
			forward -= moveSpeed;
		if (keys[SDL_SCANCODE_D])
			right += moveSpeed;
		if (keys[SDL_SCANCODE_A])
			right -= moveSpeed;

		// Consume accumulated relative motion every frame so it doesn't pile up
		// while Shift is released.
		float dx = 0.0f;
		float dy = 0.0f;
		SDL_GetRelativeMouseState(&dx, &dy);

		const bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];

		bool moved = false;
		if (forward != 0.0f || right != 0.0f)
		{
			camera.MoveAlongView(forward);
			camera.MoveAlongRight(right);
			moved = true;
		}
		if (shift && (dx != 0.0f || dy != 0.0f))
		{
			camera.RotateYawPitch(-dx * c_MouseRadiansPerPixel, -dy * c_MouseRadiansPerPixel);
			moved = true;
		}

		return moved;
	}
}
