#pragma once

namespace assetlib
{
	enum class VertexSemantic : uint8_t
	{
		kPosition,
		kNormal,
		kTangent,
		kColor,
		kTexCoord0,
		kTexCoord1,
		kJoints0,
		kWeights0
	};

	enum class VertexFormat : uint8_t
	{
		kFloat32x2,
		kFloat32x3,
		kFloat32x4,
		kUnorm8x4,
		kUnorm16x2,
		kUnorm16x4,
		kUint16x4
	};

	/** Byte size of a single attribute encoded in `format`. */
	[[nodiscard]] uint32_t
	formatSize(VertexFormat format) noexcept;

	struct VertexAttribute
	{
		VertexSemantic semantic;
		VertexFormat   format;
		uint16_t       offset;  // byte offset of this attribute within one vertex
	};

	static_assert(sizeof(VertexAttribute) == 4);

	/**
	 * Data-driven description of how to interpret an opaque interleaved vertex blob. The concrete
	 * encoding is intentionally not a fixed struct so it can evolve without breaking the format.
	 */
	struct VertexLayout
	{
		static constexpr auto c_MaxAttributes = 8u;

		std::array<VertexAttribute, c_MaxAttributes> attributes;
		uint8_t                                      attributeCount;
		uint16_t                                     stride;  // bytes per interleaved vertex
	};

	static_assert(sizeof(VertexLayout) == 36);
}
