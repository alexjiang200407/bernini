#pragma once

#include <QString>
#include <string>
#include <vector>

namespace editor
{
	struct Translation
	{
		std::string key;
		std::string locale;
		QString     text;
	};

	struct TranslationCatalog
	{
		std::string              context;
		std::vector<Translation> entries;
	};
}
