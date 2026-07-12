// THIS IS A FILE GENERATED FROM PsoType.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl
{
	enum class PsoType : uint16_t
	{
		kInvalid = uint16_t(-1),
		kOpaque_StaticMesh_Null = 0,
		kOpaque_StaticMesh_PBR = 1,
		kOpaque_StaticMesh_LoosePbr = 2,
		kAlphaTest_StaticMesh_PBR = 3,
		kAlphaTest_StaticMesh_LoosePbr = 4,
		kTransparent_StaticMesh_PBR = 5,
		kAssert_StaticMesh = 6,
		kCount = 7,
	};

	static_assert(sizeof(PsoType) == 2);

	constexpr uint16_t c_PsoCount = uint16_t(PsoType::kCount);

}
