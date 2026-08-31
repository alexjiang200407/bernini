#include "util/mesh_drop.h"

#include "util/import_outputs.h"
#include "util/mime_files.h"

#include <QFileInfo>
#include <QMessageBox>

#include <assetlib/codecs.h>

namespace editor
{
	namespace
	{
		QString
		FirstMesh(const QMimeData* mime)
		{
			return FirstLocalFileWithSuffix(mime, assetlib::c_MeshExtension);
		}

		QString
		FirstSource(const QMimeData* mime)
		{
			return FirstLocalFileWithSuffix(mime, assetlib::c_ImportedSourceExtension);
		}
	}

	bool
	IsMeshDrag(const QMimeData* mime)
	{
		return !FirstMesh(mime).isEmpty() || !FirstSource(mime).isEmpty();
	}

	MeshDrop
	MeshDroppedOn(const QMimeData* mime, const QString& dataRoot)
	{
		// A payload holding both is one model's two halves, and the container is the half that
		// reads without a project to resolve it.
		if (const QString mesh = FirstMesh(mime); !mesh.isEmpty())
			return { .mesh = mesh };

		const QString source = FirstSource(mime);
		if (source.isEmpty())
			return {};

		return { .mesh = ImportOutputsOf(dataRoot, source).mesh, .source = source };
	}

	void
	ReportUnresolved(QWidget* parent, const MeshDrop& drop)
	{
		if (!drop.mesh.isEmpty() || drop.source.isEmpty())
			return;

		const QString name = QFileInfo(drop.source).fileName();

		qWarning("MeshDrop: '%s' resolved to no mesh", qPrintable(drop.source));

		QMessageBox::warning(
			parent,
			QStringLiteral("Load Mesh"),
			QStringLiteral(
				"'%1' has no mesh to show.\n\nIt has not been imported into this "
				"project, or its import produced none.")
				.arg(name));
	}
}
