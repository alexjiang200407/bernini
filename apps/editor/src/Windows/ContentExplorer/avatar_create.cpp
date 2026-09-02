#include "avatar_create.h"

#include <assetlib/AssetStore.h>
#include <assetlib/avatar.h>
#include <core/err/util.h>

namespace editor
{
	QString
	CreateEmptyAvatar(const QString& dataRoot, const QString& skeletonKey)
	{
		const QByteArray  utf8 = skeletonKey.toUtf8();
		const std::string key  = assetlib::avatarKeyFor(
			std::string_view(utf8.constData(), static_cast<size_t>(utf8.size())));

		const assetlib::AssetStore store(std::filesystem::path(dataRoot.toStdWString()));

		core::throw_runtime_error_if(
			store.GetFiles().Stat(key).has_value(),
			"'{}' already exists; edit it rather than starting over",
			key);

		store.Save(assetlib::Avatar(), key);
		return QString::fromStdString(key);
	}
}
