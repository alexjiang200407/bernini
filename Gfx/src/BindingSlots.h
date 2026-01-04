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
		namespace CB
		{
			constexpr uint32_t FrameConstants = 0;
			constexpr uint32_t Lighting       = 0;

		}

		namespace SRV
		{
			constexpr uint32_t MeshInstance = 0;
			constexpr uint32_t MeshInfo     = 1;
			constexpr uint32_t IndexBuffer  = 2;
			constexpr uint32_t VertexBuffer = 3;
		}

		namespace UAV
		{
			constexpr uint32_t DrawIndirectArg      = 0;
			constexpr uint32_t DrawIndirectArgCount = 1;

			// Phong Material Textures
			constexpr uint32_t DiffuseTex  = 0;
			constexpr uint32_t NormalTex   = 1;
			constexpr uint32_t SpecularTex = 2;
		}
	}

	constexpr uint32_t TEXTURE_COUNT = 3;
}
