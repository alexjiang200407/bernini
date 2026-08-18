// THIS IS A FILE GENERATED FROM PsoType.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl
{
	enum class PsoType : uint32_t
	{
		kInvalid = uint32_t(-1),
		kOpaque_StaticMesh_Null = 0,
		kOpaque_StaticMesh_PBR = 1,
		kOpaque_StaticMesh_LoosePbr = 2,
		kAlphaTest_StaticMesh_PBR = 3,
		kAlphaTest_StaticMesh_LoosePbr = 4,
		kTransparent_StaticMesh_PBR = 5,
		kTransparent_StaticMesh_LoosePbr = 6,
		kHashedAlpha_StaticMesh_PBR = 7,
		kHashedAlpha_StaticMesh_LoosePbr = 8,
		kAssert_StaticMesh = 9,
		kOpaque_VatMesh_PBR = 10,
		kOpaque_SkinnedMesh_PBR = 11,
		kCount = 12,
	};

	static_assert(sizeof(PsoType) == 4);

	constexpr uint32_t c_PsoCount = uint32_t(PsoType::kCount);

}
