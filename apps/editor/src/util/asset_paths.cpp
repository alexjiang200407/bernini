#include "util/asset_paths.h"

#include <QDir>
#include <QRegularExpression>

namespace editor
{
	bool
	IsPlainFileStem(const QString& name)
	{
		if (name.isEmpty())
			return false;

		static const QRegularExpression c_Unsafe(QStringLiteral("[^A-Za-z0-9_.-]"));
		if (name.contains(c_Unsafe))
			return false;

		// "." and ".." survive the character check and are not names.
		return name != "." && name != "..";
	}

	bool
	IsContainedRelativePath(const QString& path)
	{
		if (path.isEmpty() || QDir::isAbsolutePath(path))
			return false;

		if (path.contains(':') || path.startsWith('/') || path.startsWith('\\'))
			return false;

		const QString cleaned = QDir::cleanPath(path);
		if (cleaned == ".." || cleaned.startsWith("../"))
			return false;

		return !cleaned.isEmpty() && cleaned != ".";
	}

	QString
	JoinCategory(const QString& category, const QString& path)
	{
		if (!IsContainedRelativePath(path))
			return category;

		return QStringLiteral("%1/%2").arg(category, QDir::cleanPath(path));
	}
}
