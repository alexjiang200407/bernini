#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
namespace core::str
{
#if defined(_WIN32)
	// Bridges to Win32's wide-character APIs; no other platform has one.
	std::wstring
	string_to_wide(std::string_view str);

	std::string
	wide_to_string(std::wstring_view ws);
#endif

	size_t to_utf32(std::span<char16_t>, std::span<char32_t>);

	/**
	 * `bytes` as a binary-prefixed size for a person to read: "512 B", "1.5 MiB", "2.0 GiB".
	 *
	 * Whole bytes below 1 KiB and one decimal above, because a tenth of a KiB is a distinction
	 * nobody acts on and a tenth of a GiB is one they do. Never for a value something parses back.
	 */
	[[nodiscard]] std::string
	format_bytes(uint64_t bytes);

	struct string_view_hash
	{
		using is_transparent = void;
		size_t
		operator()(std::string_view s) const noexcept
		{
			return std::hash<std::string_view>{}(s);
		}
	};

	struct string_view_eq
	{
		using is_transparent = void;
		bool
		operator()(std::string_view a, std::string_view b) const noexcept
		{
			return a == b;
		}
	};

	[[nodiscard]]
	constexpr std::pair<std::string_view, std::string_view>
	split_once(std::string_view str, std::string_view delimiter) noexcept
	{
		const auto pos = str.find(delimiter);
		if (pos == std::string_view::npos)
			return { str, std::string_view{} };
		return { str.substr(0, pos), str.substr(pos + delimiter.size()) };
	}

	struct string_comparator
	{
		using is_transparent = void;

		bool
		operator()(std::string_view a, std::string_view b) const noexcept
		{
			return a < b;
		}

		bool
		operator()(const std::string& a, std::string_view b) const noexcept
		{
			return a < b;
		}

		bool
		operator()(std::string_view a, const std::string& b) const noexcept
		{
			return a < b;
		}

		bool
		operator()(const std::string& a, const std::string& b) const noexcept
		{
			return a < b;
		}
	};

	struct transparent_string_hash
	{
		using is_transparent = void;

		size_t
		operator()(std::string_view sv) const noexcept
		{
			return std::hash<std::string_view>{}(sv);
		}
		size_t
		operator()(const std::string& s) const noexcept
		{
			return std::hash<std::string>{}(s);
		}
		size_t
		operator()(const char* s) const noexcept
		{
			return std::hash<std::string_view>{}(s);
		}
	};

	template <typename T>
	using unordered_str_map =
		std::unordered_map<std::string, T, transparent_string_hash, std::equal_to<>>;
}
