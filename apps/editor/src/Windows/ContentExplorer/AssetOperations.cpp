#include "AssetOperations.h"

#include "Windows/ContentExplorer/asset_rules.h"
#include "Windows/ContentExplorer/avatar_create.h"
#include "util/editor_language.h"
#include <assetlib/asset_refs.h>
#include <editor_sdk/BackgroundTask.h>
#include <editor_sdk/material_bake.h>
#include <editor_sdk/source_mesh.h>

#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

#include <assetlib/AssetStore.h>
#include <exception>
#include <filesystem>
#include <optional>
#include <qnamespace.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <string>
#include <string_view>
#include <utility>

AssetOperations::AssetOperations(QWidget* parent, AssetsHeldOpenFn assetsHeldOpen) :
	QObject(parent), m_Parent(parent), m_AssetsHeldOpen(std::move(assetsHeldOpen))
{}

void
AssetOperations::SetDataRoot(const QString& dataRoot)
{
	m_DataRoot = dataRoot;
}

void
AssetOperations::Bake(const QString& asset)
{
	const std::filesystem::path dataRoot = m_DataRoot.toStdWString();

	// Compositing decodes, resizes and re-encodes a KTX2 for each map, so it runs off the UI thread. It
	// touches files only, never bgl. Baking reads the material off disk, so the routes it composites are
	// the ones last saved -- Save in the Material Editor first to bake unsaved edits.
	const background::TaskResult result = background::RunWithLoadingScreen(
		m_Parent,
		editor::Localize(
			"editor.asset_operations.baking",
			{ QFileInfo(asset).fileName() },
			"Baking {0}"),
		[&](background::Progress& progress) {
			editor::BakeMaterials(assetlib::AssetStore(dataRoot), { asset }, progress);
		},
		background::Cancellable::kYes);

	// A map bakeMaterial wrote is named by the hash of its inputs, so a cancelled or re-run bake leaves
	// only correct, reusable files.
	if (result.Cancelled())
		return;

	if (result.Failed())
	{
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.bake_material_title", "Bake Material"),
			editor::Localize(
				"editor.asset_operations.bake_failed",
				{ QFileInfo(asset).fileName(), result.error },
				"Could not bake '{0}':\n\n{1}"));
		return;
	}

	// The thumbnail cache watches the material's mtime and repaints itself; the Material Editor does
	// not, and is showing what this file said before the bake.
	Q_EMIT MaterialBaked(asset);
}

void
AssetOperations::CreateAvatar(const QString& asset)
{
	const QString skeleton =
		editor::GetSourceSkeleton(m_DataRoot, QDir(m_DataRoot).filePath(asset));
	if (skeleton.isEmpty())
		return;

	try
	{
		const QString key = editor::CreateEmptyAvatar(m_DataRoot, skeleton);
		Q_EMIT AvatarCreated(QDir(m_DataRoot).filePath(key));
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.create_avatar_title", "Create Avatar"),
			editor::Localize(
				"editor.asset_operations.create_avatar_failed",
				{ QFileInfo(asset).fileName(), e.what() },
				"Could not create an avatar for '{0}':\n\n{1}"));
	}
}

bool
AssetOperations::IsHeldOpen(const QString& absolute, bool isDirectory) const
{
	return editor::IsHeldOpen(m_AssetsHeldOpen(), absolute, isDirectory);
}

void
AssetOperations::Delete(const QString& asset)
{
	DeleteWithPlanner(asset, assetlib::planDeletion);
}

void
AssetOperations::DeleteCascade(const QString& asset)
{
	DeleteWithPlanner(asset, assetlib::planCascadeDeletion);
}

void
AssetOperations::DeleteWithPlanner(
	const QString& asset,
	assetlib::DeletionPlan (*planner)(const assetlib::AssetRefGraph&, std::string_view))
{
	if (!editor::IsActionableAsset(asset))
		return;

	const QString absolute    = QDir(m_DataRoot).absoluteFilePath(asset);
	const bool    isDirectory = QFileInfo(absolute).isDir();

	if (IsHeldOpen(absolute, isDirectory))
	{
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.delete_title", "Delete"),
			isDirectory ?
				editor::Localize(
					"editor.asset_operations.delete_held_open_directory",
					{ asset },
					"'{0}' holds an asset that is open in an editor panel.\n\nClose it there "
					"first: the Material Editor's next Save would write it back, the Animation "
					"panel would go on offering it, and a viewport lit by an environment is still "
					"drawing it -- which config.json's environmentMap names unless a drop replaced "
					"it.") :
				editor::Localize(
					"editor.asset_operations.delete_held_open_file",
					{ asset },
					"'{0}' is open in an editor panel.\n\nClose it there first: the Material "
					"Editor's next Save would write it back, the Animation panel would go on "
					"offering it, and a viewport lit by an environment is still drawing it -- "
					"which config.json's environmentMap names unless a drop replaced it."));
		return;
	}

	// Built inside the worker, not beside it: an AssetStore over a root that has gone throws, and
	// out here that would leave a Qt slot rather than the loading screen's error.
	auto store = std::optional<assetlib::AssetStore>();
	auto graph = std::optional<assetlib::AssetRefGraph>();

	// The scan parses every mesh and material in the project, so it runs off the UI thread. It reads
	// assetlib only, never bgl, which is what the loading screen requires of its worker. It takes no
	// cancel token, so the screen offers no button that would not work.
	const background::TaskResult scanned = background::RunWithLoadingScreen(
		m_Parent,
		editor::Localize("editor.asset_operations.delete_title", "Delete"),
		[&](background::Progress& progress) {
			progress.Report(
				0,
				0,
				editor::Localize(
					"editor.asset_operations.checking_references",
					"Checking references..."));
			store.emplace(std::filesystem::path(m_DataRoot.toStdWString()));
			graph = assetlib::AssetRefGraph::Scan(*store);
		});

	if (!scanned.Completed())
	{
		// A mesh or material that will not parse aborts the scan, and rightly so: its references cannot
		// be known, and one of them may be the file about to be deleted.
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.delete_title", "Delete"),
			editor::Localize(
				"editor.asset_operations.delete_scan_failed",
				{ asset, scanned.error },
				"Could not work out what references '{0}', so it was not deleted:\n\n{1}"));
		return;
	}

	const assetlib::DeletionPlan plan = planner(*graph, asset.toStdString());

	if (!plan.Allowed())
	{
		auto referrers = QStringList();
		for (const assetlib::AssetRef& ref : plan.blockers)
			referrers << QString::fromStdString(ref.referrer);
		referrers.removeDuplicates();
		referrers.sort();

		const bool one = referrers.size() == 1;

		auto blocked = QMessageBox(m_Parent);
		blocked.setWindowTitle(editor::Localize("editor.asset_operations.delete_title", "Delete"));
		blocked.setIcon(QMessageBox::Warning);
		blocked.setText(
			editor::Localize(
				"editor.asset_operations.delete_blocked_text",
				{ asset },
				"'{0}' cannot be deleted."));
		blocked.setInformativeText(
			isDirectory ?
				(one ? editor::Localize(
						   "editor.asset_operations.delete_blocked_directory_one",
						   "One asset outside this folder still references something inside it. "
						   "Re-route or delete it first.") :
		               editor::Localize(
						   "editor.asset_operations.delete_blocked_directory_many",
						   { referrers.size() },
						   "{0} assets outside this folder still reference something inside it. "
						   "Re-route or delete them first.")) :
				(one ? editor::Localize(
						   "editor.asset_operations.delete_blocked_one",
						   "One asset still references it. Re-route or delete it first.") :
		               editor::Localize(
						   "editor.asset_operations.delete_blocked_many",
						   { referrers.size() },
						   "{0} assets still reference it. Re-route or delete them first.")));
		blocked.setDetailedText(referrers.join('\n'));
		blocked.exec();
		return;
	}

	// The cascade takes files the user did not click, so an open one blocks it for the reason the
	// target itself would: a panel still holding it would write it back or go on offering it.
	for (const std::string& freed : plan.cascade)
	{
		const QString member = QString::fromStdString(freed);
		if (!IsHeldOpen(QDir(m_DataRoot).absoluteFilePath(member), false))
			continue;

		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.delete_title", "Delete"),
			editor::Localize(
				"editor.asset_operations.delete_cascade_held_open",
				{ member, asset },
				"'{0}' would be deleted with '{1}', but it is open in an editor panel.\n\nClose "
				"it there first."));
		return;
	}

	auto contents = QStringList();
	for (const std::string& file : plan.contents) contents << QString::fromStdString(file);

	auto cascade = QStringList();
	for (const std::string& file : plan.cascade) cascade << QString::fromStdString(file);

	auto confirm = QMessageBox(m_Parent);
	confirm.setWindowTitle(editor::Localize("editor.asset_operations.delete_title", "Delete"));
	confirm.setIcon(QMessageBox::Warning);

	if (plan.IsDirectory())
	{
		confirm.setText(
			editor::Localize(
				"editor.asset_operations.delete_directory_confirm",
				{ asset },
				"Delete '{0}' and everything in it?"));

		const QString info =
			contents.isEmpty() ?
				(cascade.isEmpty() ?
		             editor::Localize(
						 "editor.asset_operations.delete_folder_empty",
						 "The folder is empty.\n\nThis cannot be undone.") :
		             editor::Localize(
						 "editor.asset_operations.delete_folder_empty_with_cascade",
						 { cascade.size() },
						 "The folder is empty.\n\n{0} asset(s) outside the folder are referenced "
						 "only from inside it, and will be deleted too.\n\nThis cannot be "
						 "undone.")) :
				(cascade.isEmpty() ?
		             editor::Localize(
						 "editor.asset_operations.delete_folder_contents",
						 { contents.size() },
						 "{0} file(s) will be deleted. Nothing outside the folder references any "
						 "of them.\n\nThis cannot be undone.") :
		             editor::Localize(
						 "editor.asset_operations.delete_folder_contents_with_cascade",
						 { contents.size(), cascade.size() },
						 "{0} file(s) will be deleted. Nothing outside the folder references any "
						 "of them.\n\n{1} asset(s) outside the folder are referenced only from "
						 "inside it, and will be deleted too.\n\nThis cannot be undone."));

		confirm.setInformativeText(info);
		confirm.setDetailedText((contents + cascade).join('\n'));
	}
	else if (!cascade.isEmpty())
	{
		confirm.setText(
			editor::Localize("editor.asset_operations.delete_confirm", { asset }, "Delete '{0}'?"));
		confirm.setInformativeText(
			editor::Localize(
				"editor.asset_operations.delete_with_cascade",
				{ cascade.size() },
				"Nothing references it. {0} asset(s) that nothing else references will be "
				"deleted with it.\n\nThis cannot be undone."));
		confirm.setDetailedText(cascade.join('\n'));
	}
	else if (plan.assetType == assetlib::AssetType::kMesh)
	{
		// The one kind whose deletion leaves something behind, and the user should not have to wonder
		// whether it took the materials with it.
		confirm.setText(
			editor::Localize("editor.asset_operations.delete_confirm", { asset }, "Delete '{0}'?"));
		confirm.setInformativeText(
			editor::Localize(
				"editor.asset_operations.delete_mesh_no_refs",
				"Nothing references it. The materials it uses are shared, and stay in place."));
	}
	else
	{
		confirm.setText(
			editor::Localize("editor.asset_operations.delete_confirm", { asset }, "Delete '{0}'?"));
		confirm.setInformativeText(
			editor::Localize(
				"editor.asset_operations.delete_no_refs",
				"Nothing references it. This cannot be undone."));
	}

	auto* remove = confirm.addButton(
		editor::Localize("editor.asset_operations.delete_title", "Delete"),
		QMessageBox::DestructiveRole);
	confirm.addButton(QMessageBox::Cancel);
	confirm.setDefaultButton(QMessageBox::Cancel);
	confirm.exec();

	if (confirm.clickedButton() != remove)
		return;

	const assetlib::DeletionResult result = store->DeleteAsset(plan);

	switch (result.status)
	{
	case assetlib::DeletionStatus::kDeleted:
		// The model watches the directory, so a row goes on its own -- but a view rooted *inside* what
		// just went is left showing a folder that no longer exists.
		if (isDirectory)
			Q_EMIT DirectoryDeleted(absolute);
		return;

	case assetlib::DeletionStatus::kFailed:
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.delete_title", "Delete"),
			editor::Localize(
				"editor.asset_operations.delete_failed",
				{ asset, result.error },
				"'{0}' could not be deleted:\n\n{1}\n\nIt may be open in another program."));
		return;

	case assetlib::DeletionStatus::kRefused:
		// Something wrote a reference to it between the scan and the confirmation.
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.delete_title", "Delete"),
			editor::Localize(
				"editor.asset_operations.delete_refused",
				{ asset },
				"'{0}' is referenced again, and was not deleted."));
		return;
	}
}

void
AssetOperations::Rename(const QString& asset)
{
	if (!editor::IsActionableAsset(asset))
		return;

	const QString   absolute = QDir(m_DataRoot).absoluteFilePath(asset);
	const QFileInfo info(absolute);
	const bool      isDirectory = info.isDir();

	// The extension is not offered for editing: it says what the asset is, and every stored
	// reference and menu action dispatches on it.
	const QString stem = isDirectory ? info.fileName() : info.completeBaseName();

	bool    ok = false;
	QString entered =
		QInputDialog::getText(
			m_Parent,
			editor::Localize("editor.asset_operations.rename_title", "Rename"),
			isDirectory ? editor::Localize(
							  "editor.asset_operations.directory_name_prompt",
							  "Directory name:") :
						  editor::Localize("editor.asset_operations.name_prompt", "Name:"),
			QLineEdit::Normal,
			stem,
			&ok)
			.trimmed();

	// The dialog edits the stem, but a user asked for a file's name types the extension back readily
	// enough -- taken literally that would yield 'Body.bmaterial.bmaterial'.
	const QString suffix = isDirectory ? QString() : "." + info.suffix();
	if (!suffix.isEmpty() && entered.endsWith(suffix, Qt::CaseInsensitive))
		entered.chop(suffix.size());

	if (!ok || entered.isEmpty() || entered == stem)
		return;

	if (!editor::IsValidAssetFileName(entered))
	{
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.rename_title", "Rename"),
			editor::Localize(
				"editor.asset_operations.invalid_name",
				{ entered },
				"'{0}' is not a name every platform this project is shared with can use."));
		return;
	}

	if (IsHeldOpen(absolute, isDirectory))
	{
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.rename_title", "Rename"),
			isDirectory ?
				editor::Localize(
					"editor.asset_operations.rename_held_open_directory",
					{ asset },
					"'{0}' holds an asset that is open in an editor panel.\n\nClose it there "
					"first: the Material Editor's next Save would write the old name back, and "
					"the Animation panel would go on offering it.") :
				editor::Localize(
					"editor.asset_operations.rename_held_open_file",
					{ asset },
					"'{0}' is open in an editor panel.\n\nClose it there first: the Material "
					"Editor's next Save would write the old name back, and the Animation panel "
					"would go on offering it."));
		return;
	}

	const QString newName = isDirectory ? entered : entered + "." + info.suffix();
	const int     slash   = asset.lastIndexOf('/');
	const QString to      = slash < 0 ? newName : asset.left(slash + 1) + newName;

	// Built inside the worker, not beside it: an AssetStore over a root that has gone throws, and
	// out here that would leave a Qt slot rather than the loading screen's error.
	auto store = std::optional<assetlib::AssetStore>();
	auto graph = std::optional<assetlib::AssetRefGraph>();

	// Off the UI thread for the reason Delete's scan is: it parses every mesh and material in the
	// project, reading assetlib alone.
	const background::TaskResult scanned = background::RunWithLoadingScreen(
		m_Parent,
		editor::Localize("editor.asset_operations.rename_title", "Rename"),
		[&](background::Progress& progress) {
			progress.Report(
				0,
				0,
				editor::Localize(
					"editor.asset_operations.checking_references",
					"Checking references..."));
			store.emplace(std::filesystem::path(m_DataRoot.toStdWString()));
			graph = assetlib::AssetRefGraph::Scan(*store);
		});

	if (!scanned.Completed())
	{
		// A mesh or material that will not parse aborts the scan: its references cannot be known, and
		// one of them may name the file about to move -- and would then be left pointing at nothing.
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.rename_title", "Rename"),
			editor::Localize(
				"editor.asset_operations.rename_scan_failed",
				{ asset, scanned.error },
				"Could not work out what references '{0}', so it was not renamed:\n\n{1}"));
		return;
	}

	auto plan = assetlib::RenamePlan();
	try
	{
		plan = assetlib::planRename(*graph, asset.toStdString(), to.toStdString());
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.rename_title", "Rename"),
			editor::Localize(
				"editor.asset_operations.rename_blocked",
				{ asset, e.what() },
				"'{0}' cannot be renamed:\n\n{1}"));
		return;
	}

	// A rename of an imported source moves what that import produced, and a panel holding one of
	// those is holding a file about to move out from under it. The target's own check cannot see
	// them: the file clicked was the `.glb`, and these are containers nobody named.
	for (const assetlib::RenameMove& move : plan.outputs)
	{
		const QString from = QString::fromStdString(move.from);
		if (!IsHeldOpen(QDir(m_DataRoot).absoluteFilePath(from), false))
			continue;

		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.rename_title", "Rename"),
			editor::Localize(
				"editor.asset_operations.rename_output_held_open",
				{ from },
				"'{0}' was produced by it and is open in an editor panel.\n\nClose it there "
				"first: renaming moves it, and the panel would go on offering the old path."));
		return;
	}

	// The rewrite touches files the user did not click, and an open one is held for the reason the
	// target itself would be: a re-save or a stale offer under the old path.
	auto referrers = QStringList();
	for (const assetlib::AssetRef& ref : plan.referrers)
		referrers << QString::fromStdString(ref.referrer);
	referrers.removeDuplicates();
	referrers.sort();

	for (const QString& referrer : referrers)
	{
		if (!IsHeldOpen(QDir(m_DataRoot).absoluteFilePath(referrer), false))
			continue;

		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.rename_title", "Rename"),
			editor::Localize(
				"editor.asset_operations.rename_referrer_held_open",
				{ referrer },
				"'{0}' references it and is open in an editor panel.\n\nClose it there first."));
		return;
	}

	if (!referrers.isEmpty())
	{
		auto confirm = QMessageBox(m_Parent);
		confirm.setWindowTitle(editor::Localize("editor.asset_operations.rename_title", "Rename"));
		confirm.setIcon(QMessageBox::Question);
		confirm.setText(
			editor::Localize(
				"editor.asset_operations.rename_confirm",
				{ asset, to },
				"Rename '{0}' to '{1}'?"));
		confirm.setInformativeText(
			editor::Localize(
				"editor.asset_operations.rename_confirm_info",
				{ referrers.size() },
				"{0} asset(s) reference it, and will be rewritten to the new name."));
		confirm.setDetailedText(referrers.join('\n'));

		auto* apply = confirm.addButton(
			editor::Localize("editor.asset_operations.rename_title", "Rename"),
			QMessageBox::AcceptRole);
		confirm.addButton(QMessageBox::Cancel);
		confirm.setDefaultButton(apply);
		confirm.exec();

		if (confirm.clickedButton() != apply)
			return;
	}

	auto result = assetlib::RenameResult();

	// A worker for the reason the scan gets one, only more so: rewriting a referrer round-trips the
	// whole mesh, geometry and all, where the scan read its material chunk alone. Files only, no bgl.
	const background::TaskResult renamed = background::RunWithLoadingScreen(
		m_Parent,
		editor::Localize(
			"editor.asset_operations.renaming",
			{ QFileInfo(asset).fileName() },
			"Renaming {0}"),
		[&](background::Progress& progress) {
			progress.Report(
				0,
				0,
				editor::Localize(
					"editor.asset_operations.rewriting_references",
					"Rewriting references..."));
			result = store->RenameAsset(plan);
		});

	if (!renamed.Completed())
	{
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.rename_title", "Rename"),
			editor::Localize(
				"editor.asset_operations.rename_failed",
				{ asset, renamed.error },
				"'{0}' could not be renamed:\n\n{1}"));
		return;
	}

	if (result.status == assetlib::RenameStatus::kFailed)
	{
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.rename_title", "Rename"),
			editor::Localize(
				"editor.asset_operations.rename_failed_in_use",
				{ asset, result.error },
				"'{0}' could not be renamed:\n\n{1}\n\nIt may be open in another program."));
		return;
	}

	// A view rooted at or inside the renamed folder is left showing a path that no longer exists.
	if (isDirectory)
		Q_EMIT DirectoryRenamed(absolute, QDir(m_DataRoot).absoluteFilePath(to));
}

void
AssetOperations::AddDirectory(QFileSystemModel* model, const QString& parentPath)
{
	if (parentPath.isEmpty())
		return;

	bool       ok = false;
	const auto name =
		QInputDialog::getText(
			m_Parent,
			editor::Localize("editor.asset_operations.add_directory_title", "Add Directory"),
			editor::Localize("editor.asset_operations.directory_name_prompt", "Directory name:"),
			QLineEdit::Normal,
			editor::Localize("editor.asset_operations.new_folder_default", "New Folder"),
			&ok)
			.trimmed();
	if (!ok || name.isEmpty())
		return;

	const QModelIndex parent = model->index(parentPath);

	if (!parent.isValid() || !model->mkdir(parent, name).isValid())
		QMessageBox::warning(
			m_Parent,
			editor::Localize("editor.asset_operations.add_directory_title", "Add Directory"),
			editor::Localize(
				"editor.asset_operations.add_directory_failed",
				{ name },
				"Could not create directory '{0}'."));
}
