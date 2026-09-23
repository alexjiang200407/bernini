#pragma once

#include <span>
#include <string>
#include <string_view>
namespace assetlib
{
	struct TextureEncoding;

	/**
	 * What a bake names `group`'s map by its content: `<group>_<16 hex digits of hash_string(key)>`,
	 * which names no file until an encoding is put on it. This is what a document records.
	 *
	 * One spelling for every bake, so the checkers below cannot drift from the writers. `group` must
	 * contain no underscore -- the last one in a name is what separates the hash.
	 */
	[[nodiscard]] std::string
	bakedMapContentName(std::string_view group, std::string_view key);

	/**
	 * The file one encoding of a baked map's content is stored in: `<contentName>.<tag>-<8 hex>.ktx2`,
	 * the hex covering the tag and c_TextureEncodingToken. `contentName` is what the document records
	 * -- `<group>_<16 hex>`, with whatever directory precedes it.
	 */
	[[nodiscard]] std::string
	bakedMapEncodedName(std::string_view contentName, const TextureEncoding& encoding);

	/**
	 * Whether `reference` names a file rather than a baked map's content, which names none until an
	 * encoding is put on it. The one spelling of that question.
	 */
	[[nodiscard]] bool
	namesTextureFile(std::string_view reference) noexcept;

	/**
	 * The `<group>` of a key whose last path segment is `<group>_<16 hex digits>`, or empty when it
	 * has another shape.
	 */
	[[nodiscard]] std::string_view
	bakedGroupOf(std::string_view key) noexcept;

	/**
	 * Whether `fileName` is a file one of `groups` could have been baked to, under either name shape:
	 * a content name plus an encoding suffix, or the plain `<group>_<16 hex>.ktx2` written before the
	 * two halves were split.
	 *
	 * Matching says a bake *could* have written the file, not that one did, and never that anything
	 * still references it.
	 */
	[[nodiscard]] bool
	isBakedNameAmong(std::string_view fileName, std::span<const std::string_view> groups) noexcept;
}
