#pragma once

namespace bgl
{
	constexpr uint32_t c_MaxRenderTargets = 8;
	constexpr uint32_t c_BufferCount      = 2;

	// Constants shared with the GPU (meshlet caps, instance counting-sort group
	// sizes, ...) now live in the IDL module bgl/idl/src/Constants.slang and are
	// generated into idl::c... (see idl/Constants.h). Use those directly.

	/**
	 * The struct member name for the key for the smart buffers
	 */
	constexpr std::array<std::string_view, 3> c_SmartBufferUniformIndices = { "entryBuffer"sv,
		                                                                      "packedBuffer"sv,
		                                                                      "rangeBuffer"sv };
}
