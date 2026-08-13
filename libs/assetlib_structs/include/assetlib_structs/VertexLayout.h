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

	struct VertexAttribute;
	struct VertexLayout;

	/** The attribute carrying `semantic`, or nullptr when the layout has none. */
	[[nodiscard]] const VertexAttribute*
	findAttribute(const VertexLayout& layout, VertexSemantic semantic) noexcept;

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

	/**
	 * Influences a skinned vertex carries: `kJoints0` and `kWeights0` are a vec4 pair, so both the
	 * importer that writes them and anything that decodes them work in fours.
	 */
	inline constexpr uint32_t c_InfluencesPerVertex = 4;

	/**
	 * Byte offset of `semantic` within one interleaved vertex, empty when the layout does not carry
	 * it -- which is the ordinary case, since the importer packs only what the source provided.
	 */
	[[nodiscard]] std::optional<uint16_t>
	attributeOffset(const VertexLayout& layout, VertexSemantic semantic) noexcept;
}
