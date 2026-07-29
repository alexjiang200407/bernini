#pragma once

namespace assetlib
{
	// Hex digits of the content hash in a baked map's name -- a uint64 printed as %016llx.
	inline constexpr size_t c_BakedHashDigits = 16;

	inline constexpr std::string_view c_BakedMapExtension = ".ktx2";

	/**
	 * The name a bake writes for `group`: `<group>_<16 hex digits of fnv1a(key)>.ktx2`.
	 *
	 * One spelling for every bake, so the checkers below cannot drift from the writers. `group` must
	 * contain no underscore -- the last one in a name is what separates the hash.
	 */
	[[nodiscard]] std::string
	bakedMapFileName(std::string_view group, std::string_view key);

	/**
	 * Whether `fileName` is a name bakedMapFileName could have produced for one of `groups`.
	 *
	 * Matching says a bake *could* have written the file, not that one did, and never that anything
	 * still references it.
	 */
	[[nodiscard]] bool
	isBakedNameAmong(std::string_view fileName, std::span<const std::string_view> groups) noexcept;
}
