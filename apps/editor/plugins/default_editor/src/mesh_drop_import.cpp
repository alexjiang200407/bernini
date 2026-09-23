#include "mesh_drop_import.h"

#include <QDir>
#include <QString>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_sdk/mesh_drop.h>
#include <filesystem>
#include <string>

namespace editor
{
	QString
	MeshForDrop(IEditorHost& host, const QMimeData* mime, const QString& dataRoot)
	{
		const MeshDrop drop = GetMeshDroppedOn(mime, dataRoot);
		if (!drop.mesh.isEmpty())
			return drop.mesh;

		if (drop.source.isEmpty())
			return {};

		// Everything that leaves a source unresolved -- never imported, imported into another
		// project, a document that will not parse -- is answered by importing it here. An import
		// that would overwrite what is already there is what refuses, and it says so itself.
		const std::string mesh =
			host.ImportMeshSource(std::filesystem::path(drop.source.toStdWString()));

		return mesh.empty() ? QString() : QDir(dataRoot).filePath(QString::fromStdString(mesh));
	}
}
