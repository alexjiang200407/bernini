#pragma once

#include <QString>
#include <concepts>
#include <cstdint>
#include <editor_plugin_api/ILanguageResolver.h>
#include <initializer_list>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace editor
{
	/** One value for a `{n}` field, keeping its type so `{0:.2f}` formats a number as one. */
	class TextArg
	{
	public:
		using Value = std::variant<std::string, int64_t, uint64_t, float, double>;

		TextArg(const std::string_view value) : m_Value(std::string(value)) {}
		TextArg(const char* value) : m_Value(std::string(value)) {}
		TextArg(const std::string& value) : m_Value(value) {}
		TextArg(const QString& value) : m_Value(value.toStdString()) {}
		TextArg(const std::signed_integral auto value) : m_Value(static_cast<int64_t>(value)) {}
		TextArg(const std::unsigned_integral auto value) : m_Value(static_cast<uint64_t>(value)) {}
		TextArg(const float value) : m_Value(value) {}
		TextArg(const double value) : m_Value(value) {}

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

namespace editor
{
	/**
	 * `key` in `resolver`'s locale, its `{0}`, `{1}` … fields filled from `args` by std::format
	 * (`{{` for a brace); `fallback` when the locale has no row, or when the translation does not
	 * format.
	 *
	 * `key` is `context.name`, split at its last dot: `"bernini.material.save"` is `save` in the
	 * `bernini.material` catalog.
	 *
	 * Compiled once in `editor_localize` rather than inline: std::format instantiated in every
	 * translation unit that shows text is a build cost measured in minutes on MSVC.
	 *
	 * Throws std::invalid_argument on a key with no context or more than c_MaxTextArgs arguments,
	 * and std::format_error when `fallback` itself does not format with `args`.
	 */
	[[nodiscard]] QString
	Localize(
		const ILanguageResolver& resolver,
		std::string_view         key,
		const TextArgs&          args,
		std::string_view         fallback);

	/** Localize with no `{n}` fields to fill. */
	[[nodiscard]] QString
	Localize(const ILanguageResolver& resolver, std::string_view key, std::string_view fallback);
}
