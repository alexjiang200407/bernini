// THIS IS A FILE GENERATED FROM ErrorCode.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	enum class ErrorCode : uint32_t
	{
		kUnknown = 1,
		kInvalidVertexLayout = 2,
		kInvalidSubmeshIndex = 3,
		kInvalidMeshletIndex = 4,
		kMeshletVertexOverflow = 5,
		kMeshletPrimitiveOverflow = 6,
		kInvalidVertexIndex = 7,
	};

	static_assert(sizeof(ErrorCode) == 4);

}
