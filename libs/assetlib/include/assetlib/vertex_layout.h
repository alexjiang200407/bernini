#pragma once
#include <cstdint>
#include <optional>

namespace assetlib
{
	enum class VertexFormat : uint8_t;
	enum class VertexSemantic : uint8_t;
	struct VertexAttribute;
	struct VertexLayout;

	/** Byte size of a single attribute encoded in `format`. */
	[[nodiscard]] uint32_t
	formatSize(VertexFormat format) noexcept;

	/** The attribute carrying `semantic`, or nullptr when the layout has none. */
	[[nodiscard]] const VertexAttribute*
	findAttribute(const VertexLayout& layout, VertexSemantic semantic) noexcept;

	/**
	 * Byte offset of `semantic` within one interleaved vertex, empty when the layout does not carry
	 * it -- which is the ordinary case, since the importer packs only what the source provided.
	 */
	[[nodiscard]] std::optional<uint16_t>
	attributeOffset(const VertexLayout& layout, VertexSemantic semantic) noexcept;
}
