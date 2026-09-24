#pragma once

#include <QByteArray>
#include <QString>
#include <concepts>
#include <cstddef>
#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_plugin_api/LocalizedText.h>
#include <format>
#include <qtypes.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace editor
{
	namespace localize_detail
	{
		template <typename T>
		[[nodiscard]] decltype(auto)
		Formattable(const T& value)
		{
			if constexpr (std::same_as<T, QString>)
				return value.toStdString();
			else
				return (value);
		}

		[[nodiscard]] inline QString
		Format(const std::string& text, const std::format_args args)
		{
			return QString::fromStdString(std::vformat(text, args));
		}
	}

	/**
	 * `key` in `resolver`'s locale, formatted with `args` by std::format (`{0}`, `{1}`, `{{` for a
	 * brace); `fallback` when the locale has no row, or when the translation does not format.
	 *
	 * `key` is `context.name`, split at its last dot: `"bernini.material.save"` is `save` in the
	 * `bernini.material` catalog. A `QString` argument is formatted as its UTF-8.
	 *
	 * Throws std::invalid_argument on a key with no context, and std::format_error when `fallback`
	 * itself does not format with `args`.
	 */
	template <typename... Args>
	[[nodiscard]] QString
	Localize(
		const ILanguageResolver& resolver,
		const std::string_view   key,
		const std::string_view   fallback,
		const Args&... args)
	{
		const std::size_t dot = key.rfind('.');
		if (dot == std::string_view::npos || dot == 0 || dot + 1 == key.size())
			throw std::invalid_argument("Localized key needs a context: " + std::string(key));

		const QByteArray translated =
			resolver
				.Resolve(
					{ std::string(key.substr(0, dot)),
		              std::string(key.substr(dot + 1)),
		              QString::fromUtf8(fallback.data(), static_cast<qsizetype>(fallback.size())) })
				.toUtf8();

		auto values = std::tuple(localize_detail::Formattable(args)...);
		return std::apply(
			[&](const auto&... value) {
				const std::format_args formatArgs = std::make_format_args(value...);
				try
				{
					return localize_detail::Format(translated.toStdString(), formatArgs);
				}
				catch (const std::format_error&)
				{
					return localize_detail::Format(std::string(fallback), formatArgs);
				}
			},
			values);
	}
}
