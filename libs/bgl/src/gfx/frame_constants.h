#pragma once
#include <bgpu/types/Format.h>
#include <cstdint>
#include <string_view>

namespace bgl
{
	// Swapchain images, and with it the frame-in-flight depth: the debug-readback ring and the
	// per-frame command allocators are all sized to this.
	constexpr uint32_t c_SwapchainImageCount = 2;

	// Frame Graph resource names for the active render target's own textures. Imported without a
	// namespace prefix, so every view resolves them.
	constexpr std::string_view c_BackbufferName    = "backbuffer";
	constexpr std::string_view c_MotionVectorsName = "motionVectors";
	constexpr std::string_view c_SceneColorName    = "sceneColor";
	constexpr std::string_view c_DepthName         = "depth";

	constexpr std::string_view c_HistoryName = "taaHistory";

	// The velocity buffer: RG is where the surface was last frame as a UV displacement, BA the part
	// of it the surface moved on its own -- the velocity less what the camera alone gives the same
	// world position.
	constexpr bgpu::Format     c_MotionVectorFormat = bgpu::Format::RGBA16_FLOAT;
	constexpr std::string_view c_OutlineMaskName    = "outlineMask";

	// How far above its blade's root each grass pixel stands, along up; zero where no blade drew.
	// The grass phase writes it and Blob Shadows reads it, to land a disc on a blade as on its
	// ground.
	constexpr bgpu::Format     c_GrassRootHeightFormat = bgpu::Format::R16_FLOAT;
	constexpr std::string_view c_GrassRootHeightName   = "grassRootHeight";
}
