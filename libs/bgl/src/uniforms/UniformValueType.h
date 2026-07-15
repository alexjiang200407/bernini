#pragma once

namespace bgl
{
	enum class UniformType
	{
		kArray,
		kStruct,
		kValue,
		kNull,
	};

	enum class UniformValueType
	{
		kInt,
		kInt2,
		kInt3,
		kInt4,
		kUInt,
		kUInt2,
		kDescriptorHandle = kUInt2,
		kUInt3,
		kUInt4,
		kFloat,
		kFloat2,
		kFloat3,
		kFloat4,
		kBool,
		kMat4x4,
		kNone,
	};
}
