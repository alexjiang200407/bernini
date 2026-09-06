#include "mime_files.h"

#include <QMimeData>
#include <QUrl>
#include <qnamespace.h>
#include <qobject.h>
#include <qstringview.h>
#include <qtypes.h>
#include <string_view>

namespace editor
{
	QString
	FirstLocalFileWithSuffix(const QMimeData* mime, QStringView suffix)
	{
		if (mime == nullptr || !mime->hasUrls())
			return {};

		for (const QUrl& url : mime->urls())
		{
			if (!url.isLocalFile())
				continue;

			if (QString file = url.toLocalFile(); file.endsWith(suffix, Qt::CaseInsensitive))
				return file;
		}
		return {};
	}

	QString
	FirstLocalFileWithSuffix(const QMimeData* mime, const std::string_view suffix)
	{
		const QString text =
			QString::fromUtf8(suffix.data(), static_cast<qsizetype>(suffix.size()));

		return FirstLocalFileWithSuffix(mime, text);
	}
}
