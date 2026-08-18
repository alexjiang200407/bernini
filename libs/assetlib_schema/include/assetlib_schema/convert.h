#pragma once
#include <assetlib_schema/Schema.h>

namespace assetlib::schema
{
	/**
	 * Whether every value of `stored` is representable as `wanted`: the same type, or a wider
	 * integer of the same signedness, an unsigned into a strictly wider signed, or f32 into f64.
	 * Nothing narrows, and integers and floats never cross -- a value that does not fit is data
	 * loss, and that is a hook's decision, not a copy's.
	 */
	[[nodiscard]] bool
	widens(ValueType stored, ValueType wanted) noexcept;

	/**
	 * The values of `storedBytes`, each a `stored`, rewritten as `wanted`. A same-type run is a copy.
	 *
	 * @throws std::runtime_error if `wanted` does not widen `stored`, or `storedBytes` is not a
	 *         whole number of `stored` values.
	 */
	[[nodiscard]] std::vector<std::byte>
	convertValues(ValueType stored, std::span<const std::byte> storedBytes, ValueType wanted);

	/**
	 * A run of `stored`'s layout rewritten as `wanted`'s, field by field, by name -- the reader for
	 * any file written before the current struct took shape. Per field of the wanted layout: the
	 * stored field of the same name, or of the name it was RenamedFrom, is copied if its type
	 * matches, widened if `widens` allows, recursed into if both are structs, and truncated or
	 * padded to the wanted count if both are arrays; a field the file does not carry reads as its
	 * default, or zero; a field the engine no longer has is dropped.
	 *
	 * @param stored The layout `storedBytes` is a run of, in the file's schema.
	 * @param storedBytes The elements as the file holds them, back to back.
	 * @param wanted The layout to produce, in the engine's schema.
	 * @return `storedBytes.size() / stored.GetLayout().size` elements of the wanted layout.
	 * @throws std::runtime_error naming the layout, the field and both shapes, when a field's
	 *         stored shape cannot become its wanted one -- "Submesh.material: file stores f32,
	 *         engine wants u32, no conversion" -- or `storedBytes` is not a whole number of elements.
	 */
	[[nodiscard]] std::vector<std::byte>
	convert(LayoutRef stored, std::span<const std::byte> storedBytes, LayoutRef wanted);

	/** How a field's shape reads in a message: "u16", "f32[3]", "struct VertexAttribute[8]". */
	[[nodiscard]] std::string
	fieldShape(const Schema& schema, const Field& field);
}
