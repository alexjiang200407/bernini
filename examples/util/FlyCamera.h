#pragma once

#include <bgl/Camera.h>
#include <chrono>

namespace demo
{
	class DeltaClock
	{
	public:
		float
		Tick() noexcept;

	private:
		std::chrono::steady_clock::time_point m_Last = std::chrono::steady_clock::now();
	};

	bool
	ApplyFlyCam(bgl::Camera& camera, float dt, float moveUnitsPerSecond = 3.0f) noexcept;
}
