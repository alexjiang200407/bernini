#include "drop_import.h"

#include "Import/import_pipeline.h"
#include "Windows/AssetImporter/AssetImporterDialog.h"

#include <QFileInfo>
#include <QMimeData>
#include <QUrl>

#include <assetlib/bmesh_gltf.h>

namespace editor::import
{
	bool
	IsImportableMesh(const QString& localFile)
	{
		const auto ext = QFileInfo(localFile).suffix().toLower();
		return ext == "glb" || ext == "gltf";
	}

	bool
	IsImportableEnvironment(const QString& localFile)
	{
		return QFileInfo(localFile).suffix().toLower() == "hdr";
	}

	bool
	AcceptsDrop(const QMimeData& mime)
	{
		if (!mime.hasUrls())
			return false;

		return std::ranges::any_of(mime.urls(), [](const QUrl& url) {
			return url.isLocalFile() && (IsImportableMesh(url.toLocalFile()) ||
			                             IsImportableEnvironment(url.toLocalFile()));
		});
	}

	void
	RunDrop(QWidget* parent, const QString& dataRoot, const QMimeData& mime)
	{
		for (const QUrl& url : mime.urls())
		{
			if (!url.isLocalFile())
				continue;

			const QString file = url.toLocalFile();

			if (IsImportableEnvironment(file))
			{
				if (ImportEnvironment(parent, dataRoot, file) == Outcome::kCancelled)
					break;
				continue;
			}

			if (!IsImportableMesh(file))
				continue;

			// What the file's materials are decides what the dialog may offer, so it is read before the
			// dialog is built. A file that will not parse is left to the import to report.
			auto materials = std::vector<assetlib::GltfMaterial>();
			try
			{
				materials =
					assetlib::probeGltfMaterials(std::filesystem::path(file.toStdWString()));
			}
			catch (const std::exception& e)
			{
				qWarning("Import: could not read '%s': %s", qPrintable(file), e.what());
			}

			AssetImporterDialog dialog(file, materials, dataRoot, parent);
			if (dialog.exec() != QDialog::Accepted)
				continue;

			auto options         = Options();
			options.outputs      = dialog.GetOutputs();
			options.mesh         = dialog.GetImportMesh();
			options.textures     = dialog.GetImportTextures();
			options.pbrMaterials = dialog.CanImportPbrMaterials();
			options.animations   = dialog.GetImportAnimations();

			if (ImportMesh(parent, dataRoot, file, options) == Outcome::kCancelled)
				break;
		}
	}
}
