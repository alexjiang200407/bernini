#include "baked_name.h"

#include <assetlib/image_io.h>

#include <algorithm>
#include <cassert>
#include <core/hash.h>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include "texture_encoding.h"

namespace assetlib
{
	namespace
	{
		// Hex digits of the content hash in a baked map's name -- a uint64 as 16 hex digits.
		constexpr size_t c_BakedHashDigits = 16;

		constexpr std::string_view c_BakedMapExtension = ".ktx2";
	}

	std::string
	bakedMapFileName(std::string_view group, std::string_view key)
	{
		assert(group.find('_') == std::string_view::npos);

		return std::format(
			"{}_{:016x}{}",
			group,
			core::hash_string(key, core::hash_seed()),
			c_BakedMapExtension);
	}

	std::string
	bakedMapEncodedName(std::string_view contentName, const TextureEncoding& encoding)
	{
		const std::string encodingKey =
			std::format("{}|{:016x}", encoding.tag, c_TextureEncodingToken);
		const auto digest =
			static_cast<uint32_t>(core::hash_string(encodingKey, core::hash_seed()));

		return std::format(
			"{}.{}-{:08x}{}",
			contentName,
			encoding.tag,
			digest,
			c_BakedMapExtension);
	}

	std::string_view
	bakedGroupOf(std::string_view key) noexcept
	{
		if (const size_t slash = key.rfind('/'); slash != std::string_view::npos)
			key.remove_prefix(slash + 1);

		// The group name is itself allowed no underscore, so the last one is the hash separator.
		const size_t separator = key.rfind('_');
		if (separator == std::string_view::npos || separator == 0)
			return {};

		const std::string_view digits = key.substr(separator + 1);

		const auto isHex = [](char c) noexcept {
			return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		};

		if (digits.size() != c_BakedHashDigits || !std::ranges::all_of(digits, isHex))
			return {};

		return key.substr(0, separator);
	}

	bool
	isBakedNameAmong(std::string_view fileName, std::span<const std::string_view> groups) noexcept
	{
		if (!fileName.ends_with(c_BakedMapExtension))
			return false;
		fileName.remove_suffix(c_BakedMapExtension.size());

		const std::string_view group = bakedGroupOf(fileName);
		return !group.empty() && std::ranges::find(groups, group) != groups.end();
	}
}
