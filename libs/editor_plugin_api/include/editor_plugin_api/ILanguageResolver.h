#pragma once

#include <QString>

namespace editor
{
	struct LocalizedText;

	/** Borrowed host service; all access is on the GUI thread. */
	class ILanguageResolver
	{
	public:
		virtual ~ILanguageResolver() = default;

		/** Resolve in the active locale; missing entries return the descriptor fallback. */
		[[nodiscard]] virtual QString
		Resolve(const LocalizedText& text) const = 0;
	};
}
