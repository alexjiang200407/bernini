#include <gfx/Vec3.h>

GfxVec3
operator+(const GfxVec3& a, const GfxVec3& b) noexcept
{
	return GfxVec3{ .x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z };
}

GfxVec3&
operator+=(GfxVec3& a, const GfxVec3& b) noexcept
{
	a = GfxVec3{ .x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z };
	return a;
}

GfxVec3&
operator-=(GfxVec3& a, const GfxVec3& b) noexcept
{
	a = GfxVec3{ .x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z };
	return a;
}
