#pragma once

namespace bgl
{
	constexpr uint32_t c_MaxRenderTargets = 8;

	// Swapchain images, and with it the frame-in-flight depth: the debug-readback ring and the
	// per-frame command allocators are all sized to this.
	constexpr uint32_t c_SwapchainImageCount = 2;

	// Constants shared with the GPU (meshlet caps, instance counting-sort group
	// sizes, ...) now live in the IDL module bgl/idl/src/Constants.slang and are
	// generated into idl::c... (see idl/Constants.h). Use those directly.

	/**
	 * The struct member name for the key for the smart buffers
	 */
	constexpr std::array<std::string_view, 3> c_SmartBufferUniformIndices = { "entryBuffer"sv,
		                                                                      "packedBuffer"sv,
		                                                                      "rangeBuffer"sv };

	/**
	 * Index of the sole member holding the descriptor inside an idl resource handle -- the
	 * `DescriptorHandle` in e.g. TextureHandle { Texture2D.Handle texture; }. Addressed by position
	 * rather than name because the member is spelled differently across handles (texture / sampler).
	 */
	constexpr uint32_t c_HandleUniformMember = 0;
}
