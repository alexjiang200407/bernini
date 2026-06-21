#pragma once

namespace bgl
{
	enum class PsoType : uint16_t
	{
		kInvalid               = static_cast<uint16_t>(-1),
		kOpaque_StaticMesh_PBR = 0,
		kAlphaTest_StaticMesh_PBR,
		kTransparent_StaticMesh_PBR,
		kCount,
	};
}
