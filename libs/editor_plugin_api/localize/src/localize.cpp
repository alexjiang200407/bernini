#include <QByteArray>
#include <QString>
#include <array>
#include <cstddef>
#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_plugin_api/LocalizedText.h>
#include <editor_plugin_api/localize.h>
#include <format>
#include <qtypes.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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
	namespace
	{
		template <std::size_t... I>
		[[nodiscard]] std::string
		FormatExactly(const std::string& text, const TextArgs& args, std::index_sequence<I...>)
		{
			return std::vformat(text, std::make_format_args(args.values[I]...));
		}

		// std::format takes a fixed count, so a runtime one is dispatched to the matching arity.
		[[nodiscard]] std::string
		Format(const std::string& text, const TextArgs& args)
		{
			using Formatter = std::string (*)(const std::string&, const TextArgs&);
			static constexpr auto c_Formatters = []<std::size_t... N>(std::index_sequence<N...>) {
				// The inner braces are the array's own aggregate member; MSVC's /Wall reports a
				// pack expanded straight into the outer ones as an unbraced subobject.
				return std::array<Formatter, sizeof...(N)>{ { [](const std::string& t,
					                                             const TextArgs&    a) {
					return FormatExactly(t, a, std::make_index_sequence<N>());
				}... } };
			}(std::make_index_sequence<c_MaxTextArgs + 1>());
			return c_Formatters[args.values.size()](text, args);
		}
	}

	QString
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

		const LocalizedText text{
			std::string(key.substr(0, dot)),
			std::string(key.substr(dot + 1)),
			QString::fromUtf8(fallback.data(), static_cast<qsizetype>(fallback.size()))
		};
		const QByteArray translated = resolver.Resolve(text).toUtf8();
		try
		{
			return QString::fromStdString(Format(translated.toStdString(), args));
		}
		catch (const std::format_error&)
		{
			return QString::fromStdString(Format(std::string(fallback), args));
		}
	}

	QString
	Localize(
		const ILanguageResolver& resolver,
		const std::string_view   key,
		const std::string_view   fallback)
	{
		return Localize(resolver, key, TextArgs(), fallback);
	}
}
