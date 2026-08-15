#include "mime_files.h"

#include <QMimeData>
#include <QUrl>

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
}
