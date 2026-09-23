#pragma once

#include <span>
#include <string>
#include <string_view>
namespace assetlib
{
	struct TextureEncoding;

	/**
	 * The name a bake writes for `group`: `<group>_<16 hex digits of hash_string(key)>.ktx2`.
	 *
	 * One spelling for every bake, so the checkers below cannot drift from the writers. `group` must
	 * contain no underscore -- the last one in a name is what separates the hash.
	 */
	[[nodiscard]] std::string
	bakedMapFileName(std::string_view group, std::string_view key);

	/**
	 * The file one encoding of a baked map's content is stored in: `<contentName>.<tag>-<8 hex>.ktx2`,
	 * the hex covering the tag and c_TextureEncodingToken. `contentName` is what the document records
	 * -- `<group>_<16 hex>`, with whatever directory precedes it.
	 */
	[[nodiscard]] std::string
	bakedMapEncodedName(std::string_view contentName, const TextureEncoding& encoding);

	/**
	 * The `<group>` of a key whose last path segment is `<group>_<16 hex digits>`, or empty when it
	 * has another shape.
	 */
	[[nodiscard]] std::string_view
	bakedGroupOf(std::string_view key) noexcept;

	/**
	 * Whether `fileName` is a name bakedMapFileName could have produced for one of `groups`.
	 *
	 * Matching says a bake *could* have written the file, not that one did, and never that anything
	 * still references it.
	 */
	[[nodiscard]] bool
	isBakedNameAmong(std::string_view fileName, std::span<const std::string_view> groups) noexcept;
}
