#pragma once

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace editor::detail
{
	inline bool
	IsKey(std::string_view key) noexcept
	{
		return !key.empty() && key.front() >= 'a' && key.front() <= 'z' &&
		       std::all_of(key.begin(), key.end(), [](char c) {
				   return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
			   });
	}

	inline void
	ValidateContext(std::string_view context)
	{
		while (true)
		{
			const auto dot = context.find('.');
			if (!IsKey(context.substr(0, dot)))
				throw std::runtime_error("Invalid translation context");
			if (dot == std::string_view::npos)
				return;
			context.remove_prefix(dot + 1);
		}
	}

	inline void
	ValidateLocale(std::string_view locale)
	{
		const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
		if (locale.empty() || !letter(locale.front()) ||
		    !std::all_of(locale.begin(), locale.end(), [&](char c) {
				return letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '-';
			}))
			throw std::runtime_error("Invalid translation locale");
	}
}
