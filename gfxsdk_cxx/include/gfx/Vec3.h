#include <Core/type_traits.h>
#include <gfx/ffi/common.h>

GfxVec3
operator+(const GfxVec3& a, const GfxVec3& b) noexcept;

GfxVec3&
operator+=(GfxVec3& a, const GfxVec3& b) noexcept;

GfxVec3&
operator-=(GfxVec3& a, const GfxVec3& b) noexcept;

template <core::type_traits::numeric T>
GfxVec3
operator*(const GfxVec3& a, T scalar) noexcept
{
	return GfxVec3{ a.x * static_cast<float>(scalar),
		            a.y * static_cast<float>(scalar),
		            a.z * static_cast<float>(scalar) };
}

template <core::type_traits::numeric T>
GfxVec3
operator*(T scalar, const GfxVec3& a) noexcept
{
	return a * scalar;
}
