#include "util/asset_paths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace editor
{
	qint64
	FileStamp(const QString& path)
	{
		const QFileInfo info(path);
		return info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
	}

	bool
	IsTextureFile(const QString& path)
	{
		return path.endsWith(QStringLiteral(".ktx2"), Qt::CaseInsensitive);
	}

	namespace
	{
		/** The one statement of which characters a file stem may hold. */
		const QRegularExpression&
		UnsafeStemCharacters()
		{
			static const QRegularExpression c_Unsafe(QStringLiteral("[^A-Za-z0-9_.-]"));
			return c_Unsafe;
		}
	}

	bool
	IsHiddenInExplorer(const QString& path)
	{
		return path.endsWith(QStringLiteral(".bvat"), Qt::CaseInsensitive) ||
		       path.endsWith(QStringLiteral(".bimport"), Qt::CaseInsensitive);
	}

	bool
	IsPlainFileStem(const QString& name)
	{
		if (name.isEmpty())
			return false;

		if (name.contains(UnsafeStemCharacters()))
			return false;

		// "." and ".." survive the character check and are not names.
		return name != "." && name != "..";
	}

	QString
	ToPlainFileStem(const QString& name)
	{
		QString stem = name.trimmed().replace(UnsafeStemCharacters(), QStringLiteral("_"));

		while (stem.startsWith('.')) stem.remove(0, 1);

		return stem;
	}

	QString
	GetKeyUnder(const QString& root, const QString& path)
	{
		if (root.isEmpty() || path.isEmpty())
			return {};

		// Cleaned first, so a key that climbs out and back in (`a/../../b`) is judged on where it
		// lands rather than on how it is spelt.
		const QString key = QDir::cleanPath(QDir(root).relativeFilePath(path));

		// An absolute answer means there was no relative one -- another drive on Windows.
		if (key.isEmpty() || QDir::isAbsolutePath(key) || key == ".." || key.startsWith("../"))
			return {};

		return key;
	}

	bool
	IsKeyUnder(const QString& root, const QString& path)
	{
		return !GetKeyUnder(root, path).isEmpty();
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
