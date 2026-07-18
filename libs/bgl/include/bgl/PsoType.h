// THIS IS A FILE GENERATED FROM PsoType.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl
{
	enum class PsoType : uint16_t
	{
		kInvalid                                = uint16_t(-1),
		kOpaque_StaticMesh_Null                 = 0,
		kOpaque_StaticMesh_PBR                  = 1,
		kOpaque_StaticMesh_LoosePbr             = 2,
		kAlphaTest_StaticMesh_PBR               = 3,
		kAlphaTest_StaticMesh_LoosePbr          = 4,
		kTransparent_StaticMesh_PBR             = 5,
		kTransparent_StaticMesh_LoosePbr        = 6,
		kTransparentOcclude_StaticMesh_PBR      = 7,
		kTransparentOcclude_StaticMesh_LoosePbr = 8,
		kAssert_StaticMesh                      = 9,
		kCount                                  = 10,
	};

	static_assert(sizeof(PsoType) == 2);

	constexpr uint16_t c_PsoCount = uint16_t(PsoType::kCount);

}
