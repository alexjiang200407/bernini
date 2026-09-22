#pragma once

#include <QString>
#include <editor_plugin_api/ILanguageResolver.h>
#include <string>

namespace editor
{
	/** Resolve context/key in the active locale; use fallback when no translation exists. */
	struct LocalizedText
	{
		std::string context;
		std::string key;
		QString     fallback;

		[[nodiscard]] QString
		Resolve(const ILanguageResolver& resolver) const
		{
			return resolver.Resolve(*this);
		}
	};
}
