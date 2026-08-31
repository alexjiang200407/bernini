#include "asset_rules.h"

#include "util/asset_paths.h"

#include <assetlib/Project.h>

#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QRegularExpression>

#include <assetlib/asset_refs.h>
#include <assetlib/project_layout.h>

namespace editor
{
	QString
	AssetAt(const QFileSystemModel& model, const QModelIndex& index, const QString& dataRoot)
	{
		if (!index.isValid() || dataRoot.isEmpty())
			return {};

		const QString path     = model.filePath(index);
		const QString relative = KeyUnder(dataRoot, path);

		// Something outside the project is not the project's to delete, whatever it is named -- and
		// the data root itself is not a thing inside the project either.
		if (relative.isEmpty() || relative == ".")
			return {};

		if (model.isDir(index))
		{
			return assetlib::Project::IsRequiredDirectory(relative.toStdWString()) ? QString() :
			                                                                         relative;
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
	IsActionableAsset(const QString& asset)
	{
		if (asset.isEmpty())
			return false;

		// A directory answers nullopt for either half itself, and for anything at the data root.
		// Only a location that says `Derived/` is refused; everything else is a person's.
		return assetlib::originOf(asset.toStdString()) != assetlib::AssetOrigin::kDerived;
	}

	bool
	IsHeldOpen(const QStringList& heldOpen, const QString& absolute, bool isDirectory)
	{
		const auto holds = [&](const QString& open) {
			if (open.isEmpty())
				return false;

			const QString resolved = QFileInfo(open).absoluteFilePath();

			if (!isDirectory)
				return QFileInfo(resolved) == QFileInfo(absolute);

			// The directory holds itself, so "." counts: deleting a folder takes what is open in it.
			return IsKeyUnder(absolute, resolved);
		};

		return std::ranges::any_of(heldOpen, holds);
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
