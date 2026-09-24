#pragma once

#include <QByteArray>
#include <QString>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_plugin_api/LocalizedText.h>
#include <format>
#include <initializer_list>
#include <qtypes.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace editor
{
	/** One value for a `{n}` field, keeping its type so `{0:.2f}` formats a number as one. */
	class TextArg
	{
	public:
		using Value = std::variant<std::string, int64_t, uint64_t, double>;

		TextArg(const std::string_view value) : m_Value(std::string(value)) {}
		TextArg(const char* value) : m_Value(std::string(value)) {}
		TextArg(const std::string& value) : m_Value(value) {}
		TextArg(const QString& value) : m_Value(value.toStdString()) {}
		TextArg(const std::signed_integral auto value) : m_Value(static_cast<int64_t>(value)) {}
		TextArg(const std::unsigned_integral auto value) : m_Value(static_cast<uint64_t>(value)) {}
		TextArg(const std::floating_point auto value) : m_Value(static_cast<double>(value)) {}

		[[nodiscard]] const Value&
		Get() const noexcept
		{
			return m_Value;
		}

	private:
		Value m_Value;
	};

	inline constexpr std::size_t c_MaxTextArgs = 8;

	/** The values for a localized string's `{0}`, `{1}` … fields, in order; at most c_MaxTextArgs. */
	struct TextArgs
	{
		std::vector<TextArg> values;

		TextArgs() = default;
		TextArgs(const std::initializer_list<TextArg> list) : values(list) {}
	};
}

/** Forwards a field's spec to the held value, so a number formats as a number. */
template <>
struct std::formatter<editor::TextArg, char>
{
	std::string spec;

	constexpr auto
	// NOLINTNEXTLINE(readability-identifier-naming): std::formatter's interface
	parse(std::format_parse_context& context)
	{
		auto it = context.begin();
		while (it != context.end() && *it != '}') ++it;
		spec.assign(context.begin(), it);
		return it;
	}

	auto
	// NOLINTNEXTLINE(readability-identifier-naming): std::formatter's interface
	format(const editor::TextArg& arg, std::format_context& context) const
	{
		return std::visit(
			[&](const auto& value) {
				return std::vformat_to(
					context.out(),
					"{:" + spec + "}",
					std::make_format_args(value));
			},
			arg.Get());
	}
};

namespace editor
{
	namespace localize_detail
	{
		template <std::size_t... I>
		[[nodiscard]] std::string
		FormatExactly(const std::string& text, const TextArgs& args, std::index_sequence<I...>)
		{
			return std::vformat(text, std::make_format_args(args.values[I]...));
		}

		/** std::format takes a fixed count, so a runtime one is dispatched to the matching arity. */
		[[nodiscard]] inline std::string
		Format(const std::string& text, const TextArgs& args)
		{
			using Formatter = std::string (*)(const std::string&, const TextArgs&);
			static constexpr auto c_Formatters = []<std::size_t... N>(std::index_sequence<N...>) {
				return std::array<Formatter, sizeof...(N)>{ [](const std::string& t,
					                                           const TextArgs&    a) {
					return FormatExactly(t, a, std::make_index_sequence<N>());
				}... };
			}(std::make_index_sequence<c_MaxTextArgs + 1>());
			return c_Formatters[args.values.size()](text, args);
		}
	}

	/**
	 * `key` in `resolver`'s locale, its `{0}`, `{1}` … fields filled from `args` by std::format
	 * (`{{` for a brace); `fallback` when the locale has no row, or when the translation does not
	 * format.
	 *
	 * `key` is `context.name`, split at its last dot: `"bernini.material.save"` is `save` in the
	 * `bernini.material` catalog.
	 *
	 * Throws std::invalid_argument on a key with no context or more than c_MaxTextArgs arguments,
	 * and std::format_error when `fallback` itself does not format with `args`.
	 */
	[[nodiscard]] inline QString
	Localize(
		const ILanguageResolver& resolver,
		const std::string_view   key,
		const TextArgs&          args,
		const std::string_view   fallback)
	{
		const std::size_t dot = key.rfind('.');
		if (dot == std::string_view::npos || dot == 0 || dot + 1 == key.size())
			throw std::invalid_argument("Localized key needs a context: " + std::string(key));
		if (args.values.size() > c_MaxTextArgs)
			throw std::invalid_argument(
				"Localized text takes too many arguments: " + std::string(key));

		const QByteArray translated =
			resolver
				.Resolve(
					{ std::string(key.substr(0, dot)),
		              std::string(key.substr(dot + 1)),
		              QString::fromUtf8(fallback.data(), static_cast<qsizetype>(fallback.size())) })
				.toUtf8();
		try
		{
			return QString::fromStdString(localize_detail::Format(translated.toStdString(), args));
		}
		catch (const std::format_error&)
		{
			return QString::fromStdString(localize_detail::Format(std::string(fallback), args));
		}
	}

	/** Localize with no `{n}` fields to fill. */
	[[nodiscard]] inline QString
	Localize(
		const ILanguageResolver& resolver,
		const std::string_view   key,
		const std::string_view   fallback)
	{
		return Localize(resolver, key, TextArgs(), fallback);
	}
}
