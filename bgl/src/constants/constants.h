#pragma once

namespace bgl
{
	constexpr uint32_t c_MaxRenderTargets = 8;
	constexpr uint32_t c_BufferCount      = 2;

	// Meshlet capacity limits. These MUST match the shader-side values in
	// shaders/src/util/constants.slang (cMaxVerticesPerMeshlet / cMaxPrimsPerMeshlet).
	constexpr uint32_t c_MaxVerticesPerMeshlet = 64;
	constexpr uint32_t c_MaxPrimsPerMeshlet    = 124;

	/**
	 * The struct member name for the key for the smart buffers
	 */
	constexpr std::array<std::string_view, 3> c_SmartBufferUniformIndices = { "entryBuffer"sv,
		                                                                      "packedBuffer"sv,
		                                                                      "rangeBuffer"sv };
}
