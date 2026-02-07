#include "draw_instance/SortKey.h"

namespace gfx
{
	SortKey::SortKey(
		Layer        layerID,
		GeometryType meshType,
		MaterialType matType,
		float        depthViewSpace) noexcept
	{
		// Layer:        2 bits  [62-63]
		// GeometryType: 6 bits  [56-61]
		// MaterialType: 10 bits [46-55]
		// Depth:        46 bits [00-45]

		static constexpr uint64_t LAYER_BITS    = 2;
		static constexpr uint64_t MESH_BITS     = 6;
		static constexpr uint64_t MAT_TYPE_BITS = 10;
		static constexpr uint64_t DEPTH_BITS    = 46;

		static constexpr uint64_t LAYER_MASK    = (1ULL << LAYER_BITS) - 1;
		static constexpr uint64_t MESH_MASK     = (1ULL << MESH_BITS) - 1;
		static constexpr uint64_t MAT_TYPE_MASK = (1ULL << MAT_TYPE_BITS) - 1;
		static constexpr uint64_t DEPTH_MASK    = (1ULL << DEPTH_BITS) - 1;

		union
		{
			float    f;
			uint32_t u;
		} depthCast;
		depthCast.f = depthViewSpace;

		uint32_t depthInt = depthCast.u;
		depthInt ^= (-(int32_t)(depthInt >> 31) | 0x80000000);

		uint64_t depthVal = (uint64_t)depthInt << (DEPTH_BITS - 32);

		uint64_t layerVal   = static_cast<uint8_t>(layerID) & LAYER_MASK;
		uint64_t meshVal    = static_cast<uint8_t>(meshType) & MESH_MASK;
		uint64_t matTypeVal = static_cast<uint16_t>(matType) & MAT_TYPE_MASK;

		constexpr uint64_t OFF_MAT_TYPE = DEPTH_BITS;
		constexpr uint64_t OFF_MESH     = OFF_MAT_TYPE + MAT_TYPE_BITS;
		constexpr uint64_t OFF_LAYER    = OFF_MESH + MESH_BITS;

		m_value = (layerVal << OFF_LAYER) | (meshVal << OFF_MESH) | (matTypeVal << OFF_MAT_TYPE) |
		          (depthVal);
	}
}
