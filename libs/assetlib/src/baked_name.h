#pragma once

namespace assetlib
{
	// FNV-1a. Not cryptographic: a collision would only alias two baked maps, and the inputs are
	// short, structured strings rather than adversarial ones.
	[[nodiscard]] inline uint64_t
	hash64(std::string_view text) noexcept
	{
		uint64_t hash = 1469598103934665603ull;
		for (const char c : text)
		{
			hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
			hash *= 1099511628211ull;
		}
		return hash;
	}

	// Hex digits of the content hash in a baked map's name -- a uint64 printed as %016llx.
	inline constexpr size_t c_BakedHashDigits = 16;

	inline constexpr std::string_view c_BakedMapExtension = ".ktx2";

	/**
	 * The name a bake writes for `group`: `<group>_<16 hex digits of hash64(key)>.ktx2`.
	 *
	 * One spelling for every bake, so the checkers below cannot drift from the writers. `group` must
	 * contain no underscore -- the last one in a name is what separates the hash.
	 */
	[[nodiscard]] inline std::string
	bakedMapFileName(std::string_view group, std::string_view key)
	{
		assert(group.find('_') == std::string_view::npos);

		char hex[c_BakedHashDigits + 1] = {};
		std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash64(key)));
		return std::string(group) + '_' + hex + std::string(c_BakedMapExtension);
	}

	/**
	 * Whether `fileName` is a name bakedMapFileName could have produced for one of `groups`.
	 *
	 * Matching says a bake *could* have written the file, not that one did, and never that anything
	 * still references it.
	 */
	[[nodiscard]] inline bool
	isBakedNameAmong(std::string_view fileName, std::span<const std::string_view> groups) noexcept
	{
		if (!fileName.ends_with(c_BakedMapExtension))
			return false;
		fileName.remove_suffix(c_BakedMapExtension.size());

		// The group name is itself allowed no underscore, so the last one is the hash separator.
		const size_t separator = fileName.rfind('_');
		if (separator == std::string_view::npos)
			return false;

		const std::string_view prefix = fileName.substr(0, separator);
		const std::string_view digits = fileName.substr(separator + 1);

		const auto isHex = [](char c) noexcept {
			return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		};

		if (digits.size() != c_BakedHashDigits || !std::ranges::all_of(digits, isHex))
			return false;

		return std::ranges::find(groups, prefix) != groups.end();
	}
}
