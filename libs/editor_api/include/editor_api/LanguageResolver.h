#pragma once

#include <QString>
#include <core/str/str.h>
#include <editor_api/ILanguageResolver.h>
#include <editor_api/LocalizedText.h>
#include <editor_api/TranslationCatalog.h>
#include <string>

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
		using Entries = core::str::unordered_str_map<QString>;
		core::str::unordered_str_map<Entries> m_Catalogs;
		std::string                           m_Locale = "en";
	};
}
