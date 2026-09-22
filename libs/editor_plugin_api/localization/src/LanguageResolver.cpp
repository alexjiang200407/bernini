#include <editor_plugin_api/LanguageResolver.h>

#include "translation_validation.h"

#include <QString>
#include <editor_plugin_api/LocalizedText.h>
#include <editor_plugin_api/TranslationCatalog.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
	std::string
	TranslationKey(std::string_view locale, std::string_view key)
	{
		std::string result;
		result.reserve(locale.size() + 1 + key.size());
		result.append(locale).append("/").append(key);
		return result;
	}
}

namespace editor
{
	void
	LanguageResolver::RegisterCatalog(const TranslationCatalog& catalog)
	{
		detail::ValidateContext(catalog.context);
		if (m_Catalogs.contains(catalog.context))
			throw std::runtime_error("Translation context already registered");
		Entries entries;
		for (const auto& entry : catalog.entries)
		{
			if (!detail::IsKey(entry.key))
				throw std::runtime_error("Invalid translation key");
			detail::ValidateLocale(entry.locale);
			if (entry.text.isEmpty())
				throw std::runtime_error("Empty translation; omit missing entries");
			if (!entries.emplace(TranslationKey(entry.locale, entry.key), entry.text).second)
				throw std::runtime_error("Duplicate translation key and locale");
		}
		m_Catalogs.emplace(catalog.context, std::move(entries));
	}

	void
	LanguageResolver::SetLocale(std::string locale)
	{
		detail::ValidateLocale(locale);
		m_Locale = std::move(locale);
	}

	QString
	LanguageResolver::Resolve(const LocalizedText& text) const
	{
		const auto catalog = m_Catalogs.find(text.context);
		if (catalog == m_Catalogs.end())
			return text.fallback;
		const auto entry = catalog->second.find(TranslationKey(m_Locale, text.key));
		return entry == catalog->second.end() ? text.fallback : entry->second;
	}
}
