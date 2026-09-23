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

		// Hex digits of the encoding hash in a baked map's file name.
		constexpr size_t c_EncodingHashDigits = 8;

		bool
		isHex(char c) noexcept
		{
			return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		}

		/** Whether `segment` is a `<tag>-<8 hex>` one encoding of a map is stored under. */
		bool
		isEncodingSegment(std::string_view segment) noexcept
		{
			const size_t dash = segment.rfind('-');
			if (dash == std::string_view::npos || dash == 0)
				return false;

			const std::string_view tag    = segment.substr(0, dash);
			const std::string_view digits = segment.substr(dash + 1);

			return std::ranges::all_of(
					   tag,
					   [](char c) noexcept {
						   return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
					   }) &&
			       digits.size() == c_EncodingHashDigits && std::ranges::all_of(digits, isHex);
		}
	}

	bool
	namesEncodedBakedMap(std::string_view fileName) noexcept
	{
		if (!fileName.ends_with(c_BakedMapExtension))
			return false;
		fileName.remove_suffix(c_BakedMapExtension.size());

		const size_t dot = fileName.rfind('.');
		return dot != std::string_view::npos && isEncodingSegment(fileName.substr(dot + 1));
	}

	bool
	namesTextureFile(std::string_view reference) noexcept
	{
		return reference.ends_with(c_BakedMapExtension);
	}

	std::string
	bakedMapContentName(std::string_view group, std::string_view key)
	{
		assert(group.find('_') == std::string_view::npos);

		return std::format("{}_{:016x}", group, core::hash_string(key, core::hash_seed()));
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

		// An encoding suffix, where there is one: what is left is the content name either way, so
		// one recogniser covers a map written before the two halves were split and one written since.
		// Anything else after the dot is somebody else's file -- `basecolor_<hash>.bak.ktx2` is a copy
		// somebody made, and sweeping it would be this rule deleting what it was written to protect.
		if (const size_t dot = fileName.rfind('.'); dot != std::string_view::npos)
		{
			if (!isEncodingSegment(fileName.substr(dot + 1)))
				return false;
			fileName.remove_suffix(fileName.size() - dot);
		}

		const std::string_view group = bakedGroupOf(fileName);
		return !group.empty() && std::ranges::find(groups, group) != groups.end();
	}
}
