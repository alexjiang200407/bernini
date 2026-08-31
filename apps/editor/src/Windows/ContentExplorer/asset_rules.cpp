#include "asset_rules.h"

#include "util/asset_paths.h"
#include "util/source_mesh.h"

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
		const QString relative = GetKeyUnder(dataRoot, path);

		// Something outside the project is not the project's to delete, whatever it is named -- and
		// the data root itself is not a thing inside the project either.
		if (relative.isEmpty() || relative == ".")
			return {};

		if (model.isDir(index))
		{
			return assetlib::Project::IsRequiredDirectory(relative.toStdWString()) ? QString() :
			                                                                         relative;
		}

		// An imported source is not an asset kind -- `assetTypeFromExtension` does not know a `.glb`
		// -- but it is the row standing for a model, so it is what a rename is asked of. Authored
		// only: the extension alone would also claim a `.glb` sitting in the derived half, which is
		// a file someone left there rather than a source this project imported.
		if (IsImportedSource(path) &&
		    assetlib::originOf(relative.toStdString()) == assetlib::AssetOrigin::kAuthored)
			return relative;

		const std::optional<assetlib::AssetType> type =
			assetlib::assetTypeFromExtension(path.toStdWString());

		// A source's import document is machinery, not a row: the `.glb` above is what stands for
		// the model. Refused here rather than left to the hide rule, because that lives in a view
		// and this does not -- and deleting a document alone orphans every container its `outputs`
		// names, which nothing puts back, since Reimport reads the document to find them.
		if (type == assetlib::AssetType::kImportDocument)
			return {};

		return type ? relative : QString();
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

	QString
	GetBrowseRootFor(const QString& dataRoot, const BrowseMode mode)
	{
		if (dataRoot.isEmpty())
			return {};

		const QDir root(dataRoot);

		switch (mode)
		{
		case BrowseMode::kTextures:
			return root.filePath(QString::fromUtf8(assetlib::c_SourceTexturesDirectoryName));
		case BrowseMode::kAssets:
			break;
		}

		return root.filePath(QString::fromUtf8(assetlib::c_AuthoredDirectoryName));
	}

	bool
	IsEditableMode(const BrowseMode mode)
	{
		return mode == BrowseMode::kAssets;
	}

	bool
	IsRemovableAsset(const QString& asset)
	{
		return IsActionableAsset(asset) && !IsImportedSource(asset);
	}
}
