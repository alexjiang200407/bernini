#pragma once

namespace core::str
{
	std::wstring
	string_to_wide(std::string_view str);

	std::string
	wide_to_string(std::wstring_view ws);

	size_t toUtf32(std::span<char16_t>, std::span<char32_t>);

	struct StringViewHash
	{
		using is_transparent = void;
		size_t
		operator()(std::string_view s) const noexcept
		{
			return std::hash<std::string_view>{}(s);
		}
	};

	struct StringViewEq
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
	splitOnce(std::string_view str, std::string_view delimiter) noexcept
	{
		const auto pos = str.find(delimiter);
		if (pos == std::string_view::npos)
			return { str, std::string_view{} };
		return { str.substr(0, pos), str.substr(pos + delimiter.size()) };
	}

}
