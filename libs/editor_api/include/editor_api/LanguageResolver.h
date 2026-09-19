#pragma once

#include <QString>
#include <editor_api/ILanguageResolver.h>
#include <editor_api/LocalizedText.h>
#include <editor_api/TranslationCatalog.h>
#include <map>
#include <string>
#include <utility>

namespace editor
{
	/** Host-owned implementation; plugins borrow ILanguageResolver instead of constructing this. */
	class LanguageResolver final : public ILanguageResolver
	{
	public:
		/** Reject invalid entries or duplicate contexts without changing existing catalogs. */
		void
		RegisterCatalog(const TranslationCatalog& catalog);

		/** Exact locale match, initially en; no implicit regional or source-language fallback. */
		void
		SetLocale(std::string locale);

		[[nodiscard]] QString
		Resolve(const LocalizedText& text) const override;

	private:
		using Entries = std::map<std::pair<std::string, std::string>, QString>;
		std::map<std::string, Entries> m_Catalogs;
		std::string                    m_Locale = "en";
	};
}
