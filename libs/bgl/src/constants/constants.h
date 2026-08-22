#pragma once

namespace bgl
{
	constexpr uint32_t c_MaxRenderTargets = 8;

	constexpr uint32_t c_CubeFaceCount = 6;

	// Swapchain images, and with it the frame-in-flight depth: the debug-readback ring and the
	// per-frame command allocators are all sized to this.
	constexpr uint32_t c_SwapchainImageCount = 2;

	// Frame Graph resource names for the active render target's own textures. Imported without a
	// namespace prefix, so every view resolves them.
	constexpr std::string_view c_BackbufferName    = "backbuffer"sv;
	constexpr std::string_view c_MotionVectorsName = "motionVectors"sv;
	constexpr std::string_view c_SceneColorName    = "sceneColor"sv;
	constexpr std::string_view c_DepthName         = "depth"sv;

	constexpr std::string_view c_HistoryName     = "taaHistory"sv;
	constexpr std::string_view c_OutlineMaskName = "outlineMask"sv;

	// The bone overlay's own colour and depth, both on the output grid. Imported only by a target
	// that has the overlay on, which is the only one that allocates them.
	constexpr std::string_view c_BoneOverlayName      = "boneOverlay"sv;
	constexpr std::string_view c_BoneOverlayDepthName = "boneOverlayDepth"sv;

	// Constants shared with the GPU (meshlet caps, instance counting-sort group
	// sizes, ...) now live in the IDL module bgl/idl/src/Constants.slang and are
	// generated into idl::c... (see idl/Constants.h). Use those directly.

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
