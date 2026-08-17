#include "asset_rules.h"

#include "Project/Project.h"

#include <QDir>
#include <QFileSystemModel>
#include <QRegularExpression>

#include <assetlib/asset_refs.h>

namespace editor
{
	QString
	AssetAt(const QFileSystemModel& model, const QModelIndex& index, const QString& dataRoot)
	{
		if (!index.isValid() || dataRoot.isEmpty())
			return {};

		const QString path     = model.filePath(index);
		const QString relative = QDir(dataRoot).relativeFilePath(path);

		// Something outside the project is not the project's to delete, whatever it is named.
		if (relative.isEmpty() || relative == "." || relative.startsWith(".."))
			return {};

		if (model.isDir(index))
		{
			return Project::IsRequiredDirectory(relative.toStdWString()) ? QString() : relative;
		}

		return assetlib::assetTypeFromExtension(path.toStdWString()) ? relative : QString();
	}

	bool
	IsMaterialAsset(const QString& asset)
	{
		const std::optional<assetlib::AssetType> type =
			assetlib::assetTypeFromExtension(asset.toStdWString());
		return type && *type == assetlib::AssetType::kMaterial;
	}

	bool
	IsValidAssetFileName(const QString& name)
	{
		static const QRegularExpression c_Invalid(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));

		// The DOS device names, which Windows reserves with or without an extension: NUL.ktx2 opens a
		// device, not a file.
		static const QRegularExpression c_Reserved(
			QStringLiteral(R"(^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\..*)?$)"),
			QRegularExpression::CaseInsensitiveOption);

		return !name.isEmpty() && !name.startsWith('.') && !name.endsWith('.') &&
		       !name.endsWith(' ') && !name.contains(c_Invalid) &&
		       !c_Reserved.match(name).hasMatch();
	}
}
