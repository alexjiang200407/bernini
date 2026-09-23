#pragma once
#include "types/Format.h"
#include <cstdint>

namespace bgl
{
	/**
	 * The textures a TAA history slot holds: the accumulated colour, and FSR 2's ring of the last
	 * four frames' quantized luma, which the resolve reads to tell an oscillating pixel from one
	 * that changed.
	 */
	enum class TaaHistoryPlane : uint8_t
	{
		kColor,
		kLumaRing,
	};

	inline constexpr uint32_t c_TaaHistoryPlaneCount = 2;

	[[nodiscard]] constexpr Format
	TaaHistoryFormat(TaaHistoryPlane plane) noexcept
	{
		return plane == TaaHistoryPlane::kColor ? Format::RGBA16_FLOAT : Format::RGBA8_UNORM;
	}
}
