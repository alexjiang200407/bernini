#pragma once

namespace gfx
{
	namespace BindingSpaces
	{
		constexpr uint32_t PerFrameSpace    = 0;
		constexpr uint32_t PerObjectSpace   = 1;
		constexpr uint32_t PerMaterialSpace = 2;
	}

	namespace BindingSlots
	{
		// Per-Frame Constant Buffers
		constexpr uint32_t CameraVCB   = 0;
		constexpr uint32_t LightingPCB = 0;

		// Per-Object Constant Buffers
		constexpr uint32_t TransformVCB = 0;

		// Phong Material Textures
		constexpr uint32_t DiffuseTex  = 0;
		constexpr uint32_t NormalTex   = 1;
		constexpr uint32_t SpecularTex = 2;
	}

	constexpr uint32_t TEXTURE_COUNT = 3;
}
