#pragma once

#include <editor_plugin_api/TranslationCatalog.h>
#include <string_view>

namespace editor
{
	/** UTF-8 CSV: key followed by locale columns. Empty cells are missing translations. Throws on invalid input. */
	[[nodiscard]] TranslationCatalog
	ReadTranslationCsv(std::string_view context, std::string_view csv);
}
