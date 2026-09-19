#pragma once

#include <QString>
#include <string>

namespace editor
{
	/** Resolve context/key in the active locale; use fallback when no translation exists. */
	struct LocalizedText
	{
		std::string context;
		std::string key;
		QString     fallback;
	};
}
