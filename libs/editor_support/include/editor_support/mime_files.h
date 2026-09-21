#pragma once

#include <editor_support/export.h>

#include <QString>
#include <qstringview.h>
#include <string_view>

class QMimeData;

namespace editor
{
	/**
	 * The first local file in a drag's payload whose name ends with `suffix` (case-insensitive),
	 * or an empty string. The accept filter every asset-dropping viewport shares.
	 */
	[[nodiscard]] EDITOR_SUPPORT_EXPORT QString
	FirstLocalFileWithSuffix(const QMimeData* mime, QStringView suffix);

	/** As above, spelt for the `assetlib` extension constants, which are `std::string_view`. */
	[[nodiscard]] EDITOR_SUPPORT_EXPORT QString
	FirstLocalFileWithSuffix(const QMimeData* mime, std::string_view suffix);
}
