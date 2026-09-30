#pragma once

namespace bgl
{
	struct Color
	{
		float r, g, b, a;

		Color() : r(0.f), g(0.f), b(0.f), a(0.f) {}
		Color(float c) : r(c), g(c), b(c), a(c) {}
		Color(float red, float green, float blue, float alpha) : r(red), g(green), b(blue), a(alpha)
		{}

		bool
		operator==(const Color& rhs) const
		{
			return r == rhs.r && g == rhs.g && b == rhs.b && a == rhs.a;
		}

		bool
		operator!=(const Color& rhs) const
		{
			return !(*this == rhs);
		}

		void
		GetAsFloats(float* out) const
		{
			out[0] = r;
			out[1] = g;
			out[2] = b;
			out[3] = a;
		}
	};
}
