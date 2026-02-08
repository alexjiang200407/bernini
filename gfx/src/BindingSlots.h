#pragma once

namespace gfx
{
	namespace BindingSpaces
	{
		constexpr uint32_t PerFrameSpace      = 0;
		constexpr uint32_t SortInstancesSpace = 1;
		constexpr uint32_t GBufferSpace       = 2;
	}

	namespace BindingSlots
	{
		namespace CB
		{
			constexpr uint32_t FrameConstants = 0;
			constexpr uint32_t SortConstants  = 0;
		}

		namespace SRV
		{
			constexpr uint32_t VertexMap          = 0;
			constexpr uint32_t StaticMeshInfo     = 1;
			constexpr uint32_t StaticMeshInstance = 2;
			constexpr uint32_t IndexBuffer        = 3;
			constexpr uint32_t VertexBuffer       = 4;
			constexpr uint32_t InstanceBuffer     = 5;
			constexpr uint32_t MeshletBuffer      = 6;

			constexpr uint32_t MeshInfoRedirectBuffer           = 7;
			constexpr uint32_t VertexMapRedirectBuffer          = 8;
			constexpr uint32_t IndexRedirectBuffer              = 9;
			constexpr uint32_t VertexRedirectBuffer             = 10;
			constexpr uint32_t MeshletRedirectBuffer            = 11;
			constexpr uint32_t StaticMeshInstanceRedirectBuffer = 12;

			constexpr uint32_t GroupedInstances = 0;
		}

		namespace UAV
		{
			constexpr uint32_t GroupOffsets     = 0;
			constexpr uint32_t GroupedInstances = 1;
		}
	}
}
