#include "baked_name.h"

#include <core/hash.h>

namespace assetlib
{
	std::string
	bakedMapFileName(std::string_view group, std::string_view key)
	{
		assert(group.find('_') == std::string_view::npos);

		char hex[c_BakedHashDigits + 1] = {};
		std::snprintf(
			hex,
			sizeof(hex),
			"%016llx",
			static_cast<unsigned long long>(core::hash_string(key, core::hash_seed())));
		return std::string(group) + '_' + hex + std::string(c_BakedMapExtension);
	}

	bool
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
