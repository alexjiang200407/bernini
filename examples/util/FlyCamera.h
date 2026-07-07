#pragma once

#include <bgl/bgl.h>

namespace demo
{
	class DeltaClock
	{
	public:
		float
		Tick() noexcept;

	private:
		std::chrono::steady_clock::time_point m_last = std::chrono::steady_clock::now();
	};

	bool
	ApplyFlyCam(bgl::Camera& camera, float dt, float moveUnitsPerSecond = 3.0f) noexcept;
}
