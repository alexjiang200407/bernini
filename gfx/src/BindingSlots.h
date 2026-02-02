#pragma once

namespace gfx
{
	namespace BindingSpaces
	{
		constexpr uint32_t PerFrameSpace    = 0;
		constexpr uint32_t IndirectDrawData = 1;
	}

	namespace BindingSlots
	{
		namespace CB
		{
			constexpr uint32_t FrameConstants        = 0;
			constexpr uint32_t IndirectPushConstants = 0;
		}

		namespace SRV
		{
			constexpr uint32_t VertexMap      = 0;
			constexpr uint32_t MeshInfo       = 1;
			constexpr uint32_t IndexBuffer    = 2;
			constexpr uint32_t VertexBuffer   = 3;
			constexpr uint32_t InstanceBuffer = 4;
			constexpr uint32_t MeshletBuffer  = 5;

			constexpr uint32_t MeshInfoRedirectBuffer  = 6;
			constexpr uint32_t VertexMapRedirectBuffer = 7;
			constexpr uint32_t IndexRedirectBuffer     = 8;
			constexpr uint32_t VertexRedirectBuffer    = 9;
			constexpr uint32_t MeshletRedirectBuffer   = 10;

			constexpr uint32_t VisibleMeshletIndices = 0;
			constexpr uint32_t DrawArgsBuffer        = 1;
		}

		namespace UAV
		{
			constexpr uint32_t VisibleMeshletIndices = 0;
			constexpr uint32_t DrawArgsBuffer        = 1;
		}
	}

	constexpr uint32_t TEXTURE_COUNT = 3;
}
