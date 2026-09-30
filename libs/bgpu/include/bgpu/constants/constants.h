#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace bgpu
{
	constexpr uint32_t c_MaxRenderTargets = 8;

	constexpr uint32_t c_CubeFaceCount = 6;

	/**
	 * The bindless index no resource is ever allocated, reserved by every backend's allocator.
	 *
	 * A Uniforms mirror is zero-filled, so an unwritten handle field reads as index 0. Reserving it
	 * is what stops that reading as a live resource: an unbound handle resolves to nothing on both
	 * backends instead of silently sampling whichever resource was allocated first.
	 */
	constexpr uint32_t c_UnboundDescriptorIndex = 0;

	/**
	 * The struct member name for the key for the smart buffers
	 */
	constexpr std::array<std::string_view, 5> c_SmartBufferUniformIndices = {
		{ "entryBuffer", "handleBuffer", "packedBuffer", "rangeBuffer", "rawBuffer" }
	};
}
