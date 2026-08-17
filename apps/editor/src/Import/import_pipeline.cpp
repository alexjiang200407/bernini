#include "import_pipeline.h"

#include "Async/BackgroundTask.h"
#include "Import/import_writers.h"
#include "Project/Project.h"
#include "Windows/AssetImporter/EnvironmentImporterDialog.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QStringList>

#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/env_import.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>

namespace
{
	/**
	 * Reports that an import would land on an asset that is already there and refuses it -- import
	 * never overwrites. `replaced` is whatever the import would have written over, each being
	 * destructive to import onto and none of it recoverable. The user renames or removes the
	 * existing files and imports again.
	 *
	 * @return true when something already exists, so the import must not proceed; false when nothing
	 *         collides.
	 */
	bool
	ReportImportConflict(QWidget* parent, const QString& name, const QStringList& replaced)
	{
		if (replaced.isEmpty())
			return false;

		auto message = QMessageBox(parent);
		message.setWindowTitle("Import Asset");
		message.setIcon(QMessageBox::Warning);
		message.setText(
			QString("Cannot import '%1': it would overwrite files already in the project.")
				.arg(name));
		message.setInformativeText(
			"Import never overwrites. Remove the listed files, or choose a different folder, "
			"then import again.");
		message.setDetailedText(replaced.join('\n'));
		message.exec();

		return true;
	}
}

namespace editor::import
{
	Outcome
	ImportMesh(
		QWidget*       parent,
		const QString& dataRootPath,
		const QString& sourceFile,
		const Options& options)
	{
		namespace fs = std::filesystem;

		const fs::path source   = fs::path(sourceFile.toStdWString());
		const fs::path dataRoot = fs::path(dataRootPath.toStdWString());

		// Already category-relative; the dialog is what binds a typed folder or name to its category.
		const auto under = [&](const QString& output) {
			return dataRoot / fs::path(output.toStdWString());
		};

		// writeTextures names its output tex0.ktx2, tex1.ktx2 ... by index, so every import needs its
		// own folder or the next one silently overwrites it.
		const fs::path textureDir =
			options.textures ? under(options.outputs.textureDir) : fs::path();

		// A derived material routes at the extracted textures, so it cannot come across without them.
		const bool     importMaterials = options.pbrMaterials && options.textures && options.mesh;
		const fs::path materialDir =
			importMaterials ? under(options.outputs.materialDir) : fs::path();

		const fs::path bmeshPath = under(options.outputs.mesh);

		const QString name = QFileInfo(sourceFile).fileName();

		// Sampled before a byte is written, because they decide two things: whether the import collides
		// with something already there (and must be refused), and -- if it then fails or is cancelled --
		// what may be deleted to undo it.
		std::error_code ec;
		const bool      textureDirExisted  = !textureDir.empty() && fs::exists(textureDir, ec);
		const bool      materialDirExisted = !materialDir.empty() && fs::exists(materialDir, ec);

		// Each lands in its own category directory, like the materials and textures this import also
		// writes -- not beside the mesh. `assetlib::bake` names them beside it because a baked directory
		// is its own data root; a project has a layout, and a rig is an asset in it.
		//
		// Sampled unconditionally: whether the source turns out to carry a skin is not known until it is
		// parsed, and by then a file that was already there cannot be told from one this import wrote.
		// So a static import is refused over a rig it would never write, which is the deliberate
		// direction: the alternative is parsing before asking, and refusing too often is recoverable
		// where overwriting a rig is not.
		const fs::path bskelPath = under(options.outputs.skeleton);
		const fs::path banimPath = under(options.outputs.animations);

		// Only what this import may actually write.
		auto files = std::vector<WrittenFile>();
		if (options.mesh)
		{
			files.push_back({ bmeshPath, fs::exists(bmeshPath, ec) });
			files.push_back({ bskelPath, fs::exists(bskelPath, ec) });
		}
		if (options.animations)
			files.push_back({ banimPath, fs::exists(banimPath, ec) });

		// Named one by one rather than by the folder holding them, because two imports sharing a
		// materials folder is what the dialog's per-file names are for: only a material file that is
		// already there may refuse this import, and it is also the only thing a failure may delete.
		if (importMaterials)
		{
			for (const QString& stem : options.outputs.materialStems)
			{
				if (stem.isEmpty())
					continue;

				const fs::path file = materialDir / (stem + ".bmaterial").toStdWString();
				files.push_back({ file, fs::exists(file, ec) });
			}
		}

		const std::array<WrittenDir, 2> dirs = { {
			{ textureDir, textureDirExisted, Project::c_TexturesSrcDirectoryName },
			{ materialDir, materialDirExisted, Project::c_MaterialsDirectoryName },
		} };

		auto replaced = QStringList();
		for (const WrittenFile& file : files)
			if (file.existed)
				replaced << QString::fromStdWString(file.path.wstring());

		// Only the texture folder: it is named tex0.ktx2 by index, so one already there belongs to another
		// import and sharing it would overwrite that import's files. A materials folder is shareable, and
		// its files are checked above.
		if (textureDirExisted)
			replaced << QString::fromStdWString(textureDir.wstring());

		if (ReportImportConflict(parent, name, replaced))
			return Outcome::kBlocked;

		// Parsing the glTF and, above all, Basis-supercompressing its textures take long enough to
		// freeze the editor for minutes on a large asset. None of it touches bgl, so it runs on a worker.
		auto imported = std::optional<assetlib::imp::BMeshImport>();
		auto mesh     = std::optional<assetlib::BMesh>();
		auto tangents = assetlib::TangentGenResult();

		background::TaskResult result = background::RunWithLoadingScreen(
			parent,
			QString("Importing %1").arg(name),
			[&](background::Progress& progress) {
				const assetlib::CancelToken cancel = progress.Cancellation();

				progress.Report(0, 0, QString("Parsing %1...").arg(name));
				imported = assetlib::loadFromGltf(source, cancel);

				if (options.textures)
				{
					assetlib::writeTextures(
						*imported,
						textureDir,
						[&](size_t done, size_t total) {
							progress.Report(
								static_cast<int>(done),
								static_cast<int>(total),
								QString("Compressing textures (%1 of %2)...")
									.arg(done + 1)
									.arg(total));
						},
						cancel);
				}

				// Rebuilding a whole vertex pool is not instant either, and touches neither Qt nor bgl,
				// so it belongs beside the parse rather than on the thread drawing the loading screen.
				if (options.mesh)
				{
					progress.Report(0, 0, QString("Deriving tangents..."));
					mesh     = assetlib::toBMesh(*imported);
					tangents = assetlib::generateTangents(*mesh);
				}
			},
			background::Cancellable::kYes);

		// The material graphs cannot run on the worker -- their nodes own QPixmaps, which belong to the
		// GUI thread -- so the mesh is written here too, after the materials it must name are on disk.
		if (result.Completed())
		{
			try
			{
				if (options.mesh)
				{
					if (tangents.skipped > 0)
						qWarning(
							"Import: %u submesh(es) of '%s' have no tangent and no way to derive "
							"one; "
							"a normal map on those will not render",
							tangents.skipped,
							qPrintable(name));

					WriteRig(*imported, *mesh, dataRoot, bskelPath, banimPath, options.animations);

					if (importMaterials)
						WriteMaterials(
							*imported,
							*mesh,
							dataRoot,
							materialDir,
							textureDir,
							options.outputs.materialStems);

					WriteMesh(*mesh, bmeshPath);
				}
				else if (options.animations)
				{
					WriteClips(*imported, dataRoot, banimPath);
				}

				return Outcome::kImported;
			}
			catch (const std::exception& e)
			{
				result.error = QString::fromLatin1(e.what());
			}
		}

		// A cancelled cook throws where it stood, so the textures may be half-written and the mesh may
		// name materials that never landed. Neither outcome may leave that behind for the user to trip
		// over.
		RollBack(files, dirs);

		if (result.Cancelled())
			return Outcome::kCancelled;

		QMessageBox::warning(
			parent,
			"Import Asset",
			QString("Failed to import '%1':\n\n%2").arg(name, result.error));

		return Outcome::kFailed;
	}

	Outcome
	ImportEnvironment(QWidget* parent, const QString& dataRoot, const QString& sourceFile)
	{
		const QString name = QFileInfo(sourceFile).fileName();

		EnvironmentImporterDialog dialog(sourceFile, dataRoot, parent);
		if (dialog.exec() != QDialog::Accepted)
			return Outcome::kCancelled;

		auto desc        = assetlib::EnvImportDesc();
		desc.dataRoot    = std::filesystem::path(dataRoot.toStdWString());
		desc.source      = std::filesystem::path(sourceFile.toStdWString());
		desc.name        = dialog.GetAssetName().toStdString();
		desc.sky         = dialog.ImportSky();
		desc.lighting    = dialog.ImportLighting();
		desc.environment = dialog.ImportEnvironment();

		desc.skyDir      = std::filesystem::path(dialog.GetSkyDirectory().toStdWString());
		desc.lightingDir = std::filesystem::path(dialog.GetLightingDirectory().toStdWString());
		desc.sourceDir   = std::filesystem::path(dialog.GetSourceDirectory().toStdWString());

		// Import never overwrites, here as for a mesh. The files are asked of assetlib rather than
		// rebuilt here, so the check cannot come to name different ones than the import would write.
		auto replaced = QStringList();
		for (const std::string& target : assetlib::environmentImportTargets(desc))
		{
			std::error_code ec;
			if (std::filesystem::exists(desc.dataRoot / target, ec))
				replaced << QString::fromStdString(target);
		}

		if (ReportImportConflict(parent, name, replaced))
			return Outcome::kBlocked;

		// Projecting the source and convolving it are seconds to minutes of pure CPU, and none of it
		// touches bgl -- so it runs on a worker, as the mesh import's cook does.
		auto imported = assetlib::EnvImportResult();

		const background::TaskResult result = background::RunWithLoadingScreen(
			parent,
			QString("Importing %1").arg(name),
			[&](background::Progress& progress) {
				progress.Report(0, 0, QString("Convolving %1...").arg(name));
				imported = assetlib::importEnvironment(desc, progress.Cancellation());
			},
			background::Cancellable::kYes);

		if (result.Completed())
			return Outcome::kImported;

		// Nothing to undo: a failed or cancelled importEnvironment has already taken back what it wrote.
		if (result.Cancelled())
			return Outcome::kCancelled;

		QMessageBox::warning(
			parent,
			"Import Environment",
			QString("Failed to import '%1':\n\n%2").arg(name, result.error));

		return Outcome::kFailed;
	}
}
