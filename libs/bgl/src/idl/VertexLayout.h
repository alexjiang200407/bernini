// THIS IS A FILE GENERATED FROM VertexLayout.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	enum class VertexSemantic : uint32_t
	{
		kPosition = 0,
		kNormal = 1,
		kTangent = 2,
		kColor = 3,
		kTexCoord0 = 4,
		kTexCoord1 = 5,
		kJoints0 = 6,
		kWeights0 = 7,
	};

	static_assert(sizeof(VertexSemantic) == 4);

	enum class VertexFormat : uint32_t
	{
		kFloat32x2 = 0,
		kFloat32x3 = 1,
		kFloat32x4 = 2,
		kUnorm8x4 = 3,
		kUnorm16x2 = 4,
		kUnorm16x4 = 5,
		kUint16x4 = 6,
	};

	static_assert(sizeof(VertexFormat) == 4);

	struct VertexAttribute
	{
		VertexSemantic semantic;
		VertexFormat format;
		uint16_t byteOffset;
	};

	static_assert(sizeof(VertexAttribute) == 12);
	static_assert(offsetof(VertexAttribute, semantic) == 0);
	static_assert(offsetof(VertexAttribute, format) == 4);
	static_assert(offsetof(VertexAttribute, byteOffset) == 8);

	struct VertexLayout
	{
		VertexAttribute attributes[8];
		uint16_t attributeCount;
		uint16_t stride;
	};

	static_assert(sizeof(VertexLayout) == 100);
	static_assert(offsetof(VertexLayout, attributes) == 0);
	static_assert(offsetof(VertexLayout, attributeCount) == 96);
	static_assert(offsetof(VertexLayout, stride) == 98);

}
