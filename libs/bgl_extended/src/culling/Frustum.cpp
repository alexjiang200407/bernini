#include "culling/Frustum.h"

namespace bgl
{
	FrustumPlanes
	ExtractFrustumPlanes(const glm::mat4& viewProj) noexcept
	{
		const auto row = [&viewProj](int i) {
			return glm::vec4(viewProj[0][i], viewProj[1][i], viewProj[2][i], viewProj[3][i]);
		};

		auto frustum = FrustumPlanes{ {
			row(3) + row(0),  // left
			row(3) - row(0),  // right
			row(3) + row(1),  // bottom
			row(3) - row(1),  // top
			row(2),           // near: z' >= 0 alone, because clip depth starts at 0, not -w
			row(3) - row(2),  // far
		} };

		for (glm::vec4& plane : frustum.planes)
		{
			const float length = glm::length(glm::vec3(plane));
			if (length > 0.0f)
			{
				plane /= length;
			}
			else
			{
				// A zero-area viewport -- an editor window before its first layout, say -- makes the
				// projection, and so every plane normal, zero or NaN. Emit a plane containing all of
				// space so a degenerate view culls nothing instead of crashing: culling must never
				// drop geometry a valid view would draw. (length > 0.0f is also false for NaN.)
				plane = glm::vec4(0.0f, 0.0f, 0.0f, std::numeric_limits<float>::max());
			}
		}

		return frustum;
	}

	bool
	SphereIntersectsFrustum(
		const FrustumPlanes& frustum,
		const glm::vec3&     center,
		float                radius) noexcept
	{
		for (const glm::vec4& plane : frustum.planes)
		{
			if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
			{
				return false;
			}
		}
		return true;
	}

	idl::CullView
	BuildCullView(const glm::mat4& viewProj) noexcept
	{
		auto view     = idl::CullView();
		view.viewProj = viewProj;

		const FrustumPlanes frustum = ExtractFrustumPlanes(viewProj);
		std::ranges::copy(frustum.planes, view.frustumPlanes);

		return view;
	}
}
